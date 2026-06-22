// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameStateSubsystem.h"
#include "Eclipse.h"
#include "Save/EclipseSaveGame.h"
#include "Data/EclipseChapterDefinition.h"
#include "Data/EclipseItemDefinition.h"
#include "Data/EclipseClothingDefinition.h"
#include "Data/EclipseCharacterDefinition.h"
#include "Engine/DataTable.h"
#include "HAL/IConsoleManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

void UEclipseGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("GameStateSubsystem::Initialize"));

	// Auto-load the inventory data tables if they exist on disk and the
	// designer hasn't already wired them up. The InventoryWidget reads
	// from these to render display names / descriptions / icons.
	if (!ItemTable)
	{
		ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Justin/Data/DT_Items.DT_Items"));
		UE_LOG(LogEclipse, Log, TEXT("ItemTable auto-load %s"),
			ItemTable ? TEXT("OK") : TEXT("not present (designer can author later)"));
	}
	if (!ClothingTable)
	{
		// Authoritative location is /Game/Data/DT_Clothing — try Justin
		// folder first only for legacy /Justin/* projects.
		ClothingTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Clothing.DT_Clothing"));
		if (!ClothingTable)
		{
			ClothingTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Justin/Data/DT_Clothing.DT_Clothing"));
		}
		UE_LOG(LogEclipse, Log, TEXT("ClothingTable auto-load %s"),
			ClothingTable ? TEXT("OK") : TEXT("not present (designer can author later)"));
	}
	if (!CharacterTable)
	{
		CharacterTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Justin/Data/DT_Characters.DT_Characters"));
		if (!CharacterTable)
		{
			CharacterTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Data/DT_Characters.DT_Characters"));
		}
		UE_LOG(LogEclipse, Log, TEXT("CharacterTable auto-load %s"),
			CharacterTable ? TEXT("OK") : TEXT("not present (Select Screen roster — author later)"));
	}

	// ── Fresh-game defaults ────────────────────────────────────────────
	ApplyDefaultOutfitIfEmpty();
	EnsureDefaultCharacterSelected();
}

// Seed a fallback character on a fresh game so Gender/Race aren't "unset"
// before the Select Screen exists — otherwise every IdentityGate fails and
// those dialogue branches are unreachable. A save load runs after this and
// overwrites the selection via ApplySnapshot, so this only sticks for a
// genuinely new game (or a legacy save with no SelectedCharacterId).
void UEclipseGameStateSubsystem::EnsureDefaultCharacterSelected()
{
	if (!SelectedCharacterId.IsNone()) return;          // already chosen
	if (DefaultCharacterId.IsNone())   return;          // opted out
	if (!CharacterTable)
	{
		UE_LOG(LogEclipse, Log,
			TEXT("EnsureDefaultCharacterSelected: no CharacterTable — leaving identity unset"));
		return;
	}
	if (!SelectCharacter(DefaultCharacterId))
	{
		UE_LOG(LogEclipse, Warning,
			TEXT("EnsureDefaultCharacterSelected: default '%s' not in DT_Characters — identity stays unset"),
			*DefaultCharacterId.ToString());
	}
}

// Apply baseline shirt/jeans/shoes outfit if the slot map is empty.
// Called from Initialize() (fresh game) AND from ApplySnapshot() after
// a save load — old saves that pre-date the wearables system come back
// with empty EquippedSlots, and we don't want the paperdoll to render
// naked just because the player's existing save lacks outfit data.
void UEclipseGameStateSubsystem::ApplyDefaultOutfitIfEmpty()
{
	if (EquippedSlots.Num() != 0 || EquippedClothing.Num() != 0) return;

	auto AutoEquip = [this](FName Id, EEclipseSlotType Slot)
	{
		// Only equip if the DT_Clothing row exists for this id —
		// silent skip if missing so refactors don't crash on init.
		FEclipseClothingRow Row;
		if (!GetClothingRow(Id, Row)) return;
		EquippedSlots.Add(Slot, Id);
		EquippedClothing.AddUnique(Id);
	};
	AutoEquip(TEXT("shirt"), EEclipseSlotType::Top);
	AutoEquip(TEXT("jeans"), EEclipseSlotType::Bottom);
	AutoEquip(TEXT("shoes"), EEclipseSlotType::Shoes);

	// Test wardrobe: drop a handful of unequipped wearables into the
	// inventory so the Wearables tab's AVAILABLE grid has chips to drag.
	// TODO(design): remove once real pickups populate the wardrobe.
	auto GiveUnequipped = [this](FName Id)
	{
		FEclipseClothingRow Row;
		if (!GetClothingRow(Id, Row)) return;
		if (!Inventory.Contains(Id)) Inventory.Add(Id);
	};
	GiveUnequipped(TEXT("jacket"));
	GiveUnequipped(TEXT("hoodie"));
	GiveUnequipped(TEXT("beret"));
	GiveUnequipped(TEXT("cap"));
	GiveUnequipped(TEXT("chain"));
	GiveUnequipped(TEXT("scarf"));
	GiveUnequipped(TEXT("sunglasses"));

	UE_LOG(LogEclipse, Log, TEXT("GS: applied default outfit + test wardrobe (empty slot map)"));
}

void UEclipseGameStateSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("GameStateSubsystem::Deinitialize"));
	Super::Deinitialize();
}

bool UEclipseGameStateSubsystem::AddItem(FName ItemId)
{
	if (Inventory.Num() >= InventoryMax)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inventory full!"));
		return false;
	}

	// Defensive uniquifier: if the requested id already exists in Inventory
	// we re-form it as "<base>__<N>" until it's free. The "__" separator
	// matches the runtime-id pattern produced by Pickup_Implementation, so
	// GetBaseItemId still recovers the row key for DT lookups. Guards
	// against legacy saves that pre-date Option C's runtime-id scheme
	// (two picked-up glasses both stored as bare "glass") and any caller
	// that forgets to generate a unique id.
	FName UniqueId = ItemId;
	if (Inventory.Contains(UniqueId))
	{
		const FName Base = GetBaseItemId(ItemId);
		int32 Counter = 2;
		while (Inventory.Contains(UniqueId))
		{
			UniqueId = FName(*FString::Printf(TEXT("%s__%d"), *Base.ToString(), Counter++));
		}
		UE_LOG(LogEclipse, Log, TEXT("AddItem: '%s' already held — using unique runtime id '%s'"),
			*ItemId.ToString(), *UniqueId.ToString());
	}

	Inventory.Add(UniqueId);
	// Quest flag check uses the base id so a runtime id like
	// "wristband__Item_Wristband" still flips bHasWristband on pickup.
	if (GetBaseItemId(UniqueId) == TEXT("wristband")) bHasWristband = true;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::RemoveItem(FName ItemId)
{
	const int32 Idx = Inventory.IndexOfByKey(ItemId);
	if (Idx == INDEX_NONE) return false;
	Inventory.RemoveAt(Idx);
	ItemSlotPositions.Remove(ItemId);   // free the grid slot
	// Wristband-flag bookkeeping: only flip bHasWristband off if NO other
	// inventory entry resolves to base id "wristband" (defensive — the
	// player almost never holds two, but the check is cheap and makes the
	// flag's invariant ("any wristband held → true") self-consistent).
	if (GetBaseItemId(ItemId) == TEXT("wristband"))
	{
		const bool bAnyOther = Inventory.ContainsByPredicate(
			[](const FName& Id){ return GetBaseItemId(Id) == TEXT("wristband"); });
		if (!bAnyOther) bHasWristband = false;
	}
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::EquipClothing(FName ClothingId)
{
	if (EquippedClothing.Contains(ClothingId)) return false;
	EquippedClothing.Add(ClothingId);
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::UnequipClothing(FName ClothingId)
{
	const int32 N = EquippedClothing.Remove(ClothingId);
	if (N > 0)
	{
		ItemSlotPositions.Remove(ClothingId);
		NotifyChanged();
	}
	return N > 0;
}

// ── Slot-based equip / unequip ─────────────────────────────────────────────

bool UEclipseGameStateSubsystem::EquipClothingToSlot(FName ClothingId)
{
	// Resolve the target slot from DT_Clothing — chips don't know their
	// own slot, the DT row does. Strip the runtime "__N" suffix in case
	// the chip was uniquified on pickup.
	FEclipseClothingRow Row;
	if (!GetClothingRow(ClothingId, Row))
	{
		UE_LOG(LogEclipse, Warning,
			TEXT("EquipClothingToSlot: no DT_Clothing row for '%s'"), *ClothingId.ToString());
		return false;
	}
	const EEclipseSlotType Slot = Row.SlotType;

	// The chip must be in the inventory grid before it can be equipped.
	const int32 InvIdx = Inventory.IndexOfByKey(ClothingId);
	if (InvIdx == INDEX_NONE)
	{
		UE_LOG(LogEclipse, Warning,
			TEXT("EquipClothingToSlot: '%s' not in inventory"), *ClothingId.ToString());
		return false;
	}

	// Swap out whatever's currently in the slot (back to inventory) before
	// moving the new chip in. One wearable per slot, always.
	if (FName* Existing = EquippedSlots.Find(Slot))
	{
		if (!Existing->IsNone() && *Existing != ClothingId)
		{
			Inventory.Add(*Existing);
			EquippedClothing.Remove(*Existing);
			UE_LOG(LogEclipse, Log,
				TEXT("EquipClothingToSlot: slot %d previously had '%s' → back to inventory"),
				(int32)Slot, *Existing->ToString());
		}
	}

	// Move ClothingId out of the inventory grid into the slot.
	Inventory.RemoveAt(InvIdx);
	ItemSlotPositions.Remove(ClothingId);
	EquippedSlots.Add(Slot, ClothingId);
	if (!EquippedClothing.Contains(ClothingId))
	{
		EquippedClothing.Add(ClothingId);
	}

	UE_LOG(LogEclipse, Log,
		TEXT("EquipClothingToSlot: '%s' → slot %d"),
		*ClothingId.ToString(), (int32)Slot);
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::UnequipSlot(EEclipseSlotType Slot)
{
	FName* Existing = EquippedSlots.Find(Slot);
	if (!Existing || Existing->IsNone()) return false;

	const FName ClothingId = *Existing;
	EquippedSlots.Remove(Slot);
	EquippedClothing.Remove(ClothingId);
	Inventory.Add(ClothingId);

	UE_LOG(LogEclipse, Log,
		TEXT("UnequipSlot: slot %d → '%s' back to inventory"),
		(int32)Slot, *ClothingId.ToString());
	NotifyChanged();
	return true;
}

FName UEclipseGameStateSubsystem::GetEquippedInSlot(EEclipseSlotType Slot) const
{
	if (const FName* Existing = EquippedSlots.Find(Slot))
	{
		return *Existing;
	}
	return NAME_None;
}

// ── Currency ──

void UEclipseGameStateSubsystem::AddCoins(int32 Amount)
{
	if (Amount <= 0) return;
	Coins += Amount;
	UE_LOG(LogEclipse, Log, TEXT("Currency: +%d coins → %d total"), Amount, Coins);
	NotifyChanged();
}

bool UEclipseGameStateSubsystem::RemoveCoins(int32 Amount)
{
	if (Amount <= 0) return true;
	if (Coins < Amount)
	{
		UE_LOG(LogEclipse, Log, TEXT("Currency: cannot spend %d coins (have %d)"), Amount, Coins);
		return false;
	}
	Coins -= Amount;
	UE_LOG(LogEclipse, Log, TEXT("Currency: -%d coins → %d total"), Amount, Coins);
	NotifyChanged();
	return true;
}

void UEclipseGameStateSubsystem::AddNotes(int32 Amount)
{
	if (Amount <= 0) return;
	Notes += Amount;
	UE_LOG(LogEclipse, Log, TEXT("Currency: +%d notes → %d total"), Amount, Notes);
	NotifyChanged();
}

bool UEclipseGameStateSubsystem::RemoveNotes(int32 Amount)
{
	if (Amount <= 0) return true;
	if (Notes < Amount)
	{
		UE_LOG(LogEclipse, Log, TEXT("Currency: cannot spend %d notes (have %d)"), Amount, Notes);
		return false;
	}
	Notes -= Amount;
	UE_LOG(LogEclipse, Log, TEXT("Currency: -%d notes → %d total"), Amount, Notes);
	NotifyChanged();
	return true;
}

void UEclipseGameStateSubsystem::SetItemSlot(FName ItemId, int32 SlotIndex)
{
	if (ItemId.IsNone() || SlotIndex < 0) return;
	const int32* Existing = ItemSlotPositions.Find(ItemId);
	if (Existing && *Existing == SlotIndex) return;   // no-op
	ItemSlotPositions.Add(ItemId, SlotIndex);
	NotifyChanged();
}

void UEclipseGameStateSubsystem::SwapItemSlots(FName ItemA, int32 SlotA, FName ItemB, int32 SlotB)
{
	if (ItemA.IsNone() || ItemB.IsNone() || ItemA == ItemB) return;
	// Seed missing entries from the visual slots that the inventory UI
	// passed in — that way swapping items the player has never moved still
	// produces a stable map (instead of one item snapping back to slot 0).
	if (!ItemSlotPositions.Contains(ItemA) && SlotA >= 0) ItemSlotPositions.Add(ItemA, SlotA);
	if (!ItemSlotPositions.Contains(ItemB) && SlotB >= 0) ItemSlotPositions.Add(ItemB, SlotB);

	const int32 PrevA = ItemSlotPositions.FindRef(ItemA);
	const int32 PrevB = ItemSlotPositions.FindRef(ItemB);
	ItemSlotPositions.Add(ItemA, PrevB);
	ItemSlotPositions.Add(ItemB, PrevA);
	NotifyChanged();
}

// Shared helper — pull `ItemId` out of `Array` and re-insert at NewIdx.
// Returns true iff the array actually changed order. Centralised here so
// both reorder entry-points share the (slightly fiddly) shift-after-remove
// math; getting that wrong silently corrupts the order in subtle ways.
static bool ReorderArray(TArray<FName>& Array, FName ItemId, int32 NewIdx)
{
	const int32 OldIdx = Array.IndexOfByKey(ItemId);
	if (OldIdx == INDEX_NONE) return false;
	if (Array.Num() <= 1)     return false;

	// Clamp to a valid post-remove insertion point. Remember NewIdx is
	// expressed against the *current* array (before removal); after we
	// strip the item, anything to the right shifts left by one.
	int32 Target = FMath::Clamp(NewIdx, 0, Array.Num() - 1);
	if (Target == OldIdx) return false;

	Array.RemoveAt(OldIdx);
	if (Target > OldIdx) --Target;          // adjust for the gap left behind
	Array.Insert(ItemId, Target);
	return true;
}

bool UEclipseGameStateSubsystem::ReorderInventory(FName ItemId, int32 NewIdx)
{
	if (!ReorderArray(Inventory, ItemId, NewIdx)) return false;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::ReorderEquippedClothing(FName ClothingId, int32 NewIdx)
{
	if (!ReorderArray(EquippedClothing, ClothingId, NewIdx)) return false;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::UseItem(FName ItemId)
{
	if (!Inventory.Contains(ItemId)) return false;

	// Resolve the row so we can read effects + quest flags. If we don't
	// have a table yet, just remove the item — designers can wire the
	// table later.
	FEclipseItemRow Row;
	const bool bHasRow = GetItemRow(ItemId, Row);
	if (bHasRow)
	{
		// Key items can't be used — they're held until a dialogue / quest
		// beat consumes them. The inventory UI greys USE for these too,
		// but enforce here as well so any caller is safe.
		if (Row.Type == EEclipseItemType::Key)
		{
			UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' refused — Key item, can't be used directly"),
				*ItemId.ToString());
			return false;
		}

		// Usable: applies the three signed int meter deltas. Sweet-spot
		// orientation:
		//   HEAT         + warms, - cools
		//   THIRST       + hydrates (toward sloshing), - dries
		//   STIMULATION  + stimulates (toward tweaking), - calms
		//
		// At least one delta must be non-zero OR the legacy RestoreThirst
		// fallback must be > 0 — otherwise the item is treated as an
		// "empty container" and the use is refused (so empty baggies /
		// glasses don't vanish into nothing when the player clicks USE).
		//
		// (Other Effect fields like HeatGainMult / CoolRate are equip-time
		// modifiers, applied while an Equippable item is worn — wired up
		// in a future milestone.)
		if (Row.Type == EEclipseItemType::Usable)
		{
			const bool bAnyDelta =
				(Row.Effect.HeatDelta        != 0) ||
				(Row.Effect.ThirstDelta      != 0) ||
				(Row.Effect.StimulationDelta != 0);
			const bool bHasLegacy = (Row.Effect.RestoreThirst > 0.f);

			if (!bAnyDelta && !bHasLegacy)
			{
				UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' refused — Usable but no effect (empty container)"),
					*ItemId.ToString());
				return false;
			}

			if (bAnyDelta)
			{
				if (Row.Effect.HeatDelta        != 0) ChangeHeat       (Row.Effect.HeatDelta);
				if (Row.Effect.ThirstDelta      != 0) ChangeThirst     (Row.Effect.ThirstDelta);
				if (Row.Effect.StimulationDelta != 0) ChangeStimulation(Row.Effect.StimulationDelta);
			}
			else
			{
				// Legacy 0..100-scale value → +N hydration on the new
				// 0..10 scale. (Pre-refactor DT rows assumed "high thirst
				// = hydrated" which matches the new orientation.)
				const int32 LegacyDelta = FMath::Max(1, FMath::RoundToInt(Row.Effect.RestoreThirst / 10.f));
				ChangeThirst(LegacyDelta);
			}
		}

		UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' (type=%d quest='%s')"),
			*ItemId.ToString(), (int32)Row.Type, *Row.QuestFlag.ToString());
	}

	return RemoveItem(ItemId);
}

int32 UEclipseGameStateSubsystem::GetStatValue(FName StatKey) const
{
	// Match on lowercase. Designer-authored skill checks pass keys like
	// "aesthetics" / "rhythm" / "zen". (Stimulation moved out of the stat
	// system into the meter system — see GetMeterValue for it.)
	if (StatKey == TEXT("aesthetics"))   return Aesthetics;
	if (StatKey == TEXT("rhythm"))       return Rhythm;
	if (StatKey == TEXT("zen"))          return Zen;
	if (StatKey == TEXT("psychedelics")) return Psychedelics;
	return 0;
}

void UEclipseGameStateSubsystem::ApplyStatDelta(FName StatKey, int32 Delta)
{
	// Mirror of GetStatValue's switch — keys are lowercase. Clamp to >= 0
	// so a "-3 ZEN" against a Zen=1 player floors at 0 rather than going
	// negative. Stage-directions parser feeds us the lowercased key.
	int32* Field = nullptr;
	if      (StatKey == TEXT("aesthetics"))   Field = &Aesthetics;
	else if (StatKey == TEXT("rhythm"))       Field = &Rhythm;
	else if (StatKey == TEXT("zen"))          Field = &Zen;
	else if (StatKey == TEXT("psychedelics")) Field = &Psychedelics;

	if (!Field)
	{
		UE_LOG(LogEclipse, Warning, TEXT("ApplyStatDelta: unknown stat key '%s' (delta %d ignored)"),
			*StatKey.ToString(), Delta);
		return;
	}
	const int32 Before = *Field;
	*Field = FMath::Max(0, *Field + Delta);
	UE_LOG(LogEclipse, Log, TEXT("ApplyStatDelta: %s %d %+d -> %d"),
		*StatKey.ToString(), Before, Delta, *Field);
	NotifyChanged();
}

// ── Hidden social stats (Gender / Race / Annoyance) ────────────────────────

FName UEclipseGameStateSubsystem::GetIdentityValue(FName IdentityKey) const
{
	if (IdentityKey == TEXT("gender")) return Gender;
	if (IdentityKey == TEXT("race"))   return Race;
	return NAME_None;
}

int32 UEclipseGameStateSubsystem::GetHiddenStatValue(FName Key) const
{
	if (Key == TEXT("annoyance")) return Annoyance;
	return 0;
}

void UEclipseGameStateSubsystem::ChangeHiddenStat(FName Key, int32 Delta)
{
	if (Key == TEXT("annoyance"))
	{
		const int32 Before = Annoyance;
		Annoyance = FMath::Clamp(Annoyance + Delta, 0, AnnoyanceMax);
		UE_LOG(LogEclipse, Log, TEXT("ChangeHiddenStat: annoyance %d %+d -> %d"),
			Before, Delta, Annoyance);
		NotifyChanged();
		return;
	}
	UE_LOG(LogEclipse, Warning, TEXT("ChangeHiddenStat: unknown key '%s' (delta %d ignored)"),
		*Key.ToString(), Delta);
}

void UEclipseGameStateSubsystem::SetGender(FName NewGender)
{
	Gender = NewGender;
	UE_LOG(LogEclipse, Log, TEXT("SetGender: %s"), *Gender.ToString());
	NotifyChanged();
}

void UEclipseGameStateSubsystem::SetRace(FName NewRace)
{
	Race = NewRace;
	UE_LOG(LogEclipse, Log, TEXT("SetRace: %s"), *Race.ToString());
	NotifyChanged();
}

void UEclipseGameStateSubsystem::ChangeAnnoyance(int32 Delta)
{
	ChangeHiddenStat(TEXT("annoyance"), Delta);
}

// ── Character selection (Select Screen framework) ──────────────────────────

bool UEclipseGameStateSubsystem::GetCharacterRow(FName CharacterId, FEclipseCharacterRow& OutRow) const
{
	if (!CharacterTable || CharacterId.IsNone()) return false;
	const FEclipseCharacterRow* Found =
		CharacterTable->FindRow<FEclipseCharacterRow>(CharacterId, TEXT("SelectCharacter"));
	if (!Found) return false;
	OutRow = *Found;
	return true;
}

bool UEclipseGameStateSubsystem::SelectCharacter(FName CharacterId)
{
	FEclipseCharacterRow Row;
	if (!GetCharacterRow(CharacterId, Row))
	{
		UE_LOG(LogEclipse, Warning,
			TEXT("SelectCharacter: '%s' not found in DT_Characters (state unchanged)"),
			*CharacterId.ToString());
		return false;
	}

	SelectedCharacterId = CharacterId;
	Gender    = Row.Gender;
	Race      = Row.Race;
	Annoyance = FMath::Clamp(Row.StartingAnnoyance, 0, AnnoyanceMax);

	UE_LOG(LogEclipse, Log,
		TEXT("SelectCharacter: '%s' → gender=%s race=%s annoyance=%d"),
		*CharacterId.ToString(), *Gender.ToString(), *Race.ToString(), Annoyance);
	NotifyChanged();
	return true;
}

FName UEclipseGameStateSubsystem::GetBaseItemId(FName MaybeRuntimeId)
{
	// Runtime ids look like "<base>__<actor-name>" — see
	// AEclipseItemActor::Pickup_Implementation. Strip everything from the
	// first "__" onward; if no separator is present, the input is already a
	// base id (e.g. "wristband", "drink") and is returned unchanged.
	const FString S = MaybeRuntimeId.ToString();
	int32 SepIdx = INDEX_NONE;
	if (S.FindChar(TEXT('_'), SepIdx))
	{
		// Look for the literal "__" separator (not single underscores inside
		// names like "STALL_VOICE_CALM" or pre-existing "Item_baggie1").
		const int32 DoubleSep = S.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (DoubleSep != INDEX_NONE)
		{
			return FName(*S.Left(DoubleSep));
		}
	}
	return MaybeRuntimeId;
}

bool UEclipseGameStateSubsystem::GetItemRow(FName ItemId, FEclipseItemRow& OutRow) const
{
	if (!ItemTable) return false;
	// Try the id as-is first (covers legacy saves + unique row ids the user
	// hand-typed before per-instance ids landed). Fall back to the base id
	// extracted from the "<base>__<suffix>" runtime form.
	const FEclipseItemRow* Found = ItemTable->FindRow<FEclipseItemRow>(ItemId, TEXT("InventoryUI"));
	if (!Found)
	{
		const FName Base = GetBaseItemId(ItemId);
		if (!Base.IsNone() && Base != ItemId)
		{
			Found = ItemTable->FindRow<FEclipseItemRow>(Base, TEXT("InventoryUI"));
		}
	}
	if (!Found) return false;
	OutRow = *Found;
	return true;
}

bool UEclipseGameStateSubsystem::GetClothingRow(FName ClothingId, FEclipseClothingRow& OutRow) const
{
	if (!ClothingTable) return false;
	const FEclipseClothingRow* Found = ClothingTable->FindRow<FEclipseClothingRow>(ClothingId, TEXT("InventoryUI"));
	if (!Found)
	{
		const FName Base = GetBaseItemId(ClothingId);
		if (!Base.IsNone() && Base != ClothingId)
		{
			Found = ClothingTable->FindRow<FEclipseClothingRow>(Base, TEXT("InventoryUI"));
		}
	}
	if (!Found) return false;
	OutRow = *Found;
	return true;
}

// ── Life meters (Heat / Thirst / Stimulation) ──────────────────────────────
//
// Sweet-spot 0..10 model. Meters do NOT drain over time — they only move
// when consumables, dialogue effects, or other explicit events push them
// via ChangeMeter / ChangeXxx. Death only triggers at Stimulation == 0;
// Heat/Thirst extremes are dialogue-gates and HUD-tint cues, not killers.

int32 UEclipseGameStateSubsystem::GetMeterValue(FName MeterKey) const
{
	if (MeterKey == TEXT("heat"))        return Heat;
	if (MeterKey == TEXT("thirst"))      return Thirst;
	if (MeterKey == TEXT("stimulation")) return Stimulation;
	return 0;
}

void UEclipseGameStateSubsystem::ChangeMeter(FName MeterKey, int32 Delta)
{
	int32* Field = nullptr;
	if      (MeterKey == TEXT("heat"))        Field = &Heat;
	else if (MeterKey == TEXT("thirst"))      Field = &Thirst;
	else if (MeterKey == TEXT("stimulation")) Field = &Stimulation;

	if (!Field)
	{
		UE_LOG(LogEclipse, Warning, TEXT("ChangeMeter: unknown meter '%s' (delta %d ignored)"),
			*MeterKey.ToString(), Delta);
		return;
	}

	const int32 Before = *Field;
	*Field = FMath::Clamp(*Field + Delta, 0, MeterMax);
	UE_LOG(LogEclipse, Log, TEXT("ChangeMeter: %s %d %+d -> %d"),
		*MeterKey.ToString(), Before, Delta, *Field);

	// Death is single-shot on the Stimulation 0 transition (no re-fire if
	// the meter is repeatedly pushed past zero while already at 0).
	if (Field == &Stimulation && Before > 0 && *Field == 0)
	{
		UE_LOG(LogEclipse, Log, TEXT("Stimulation reached 0 — player died"));
		OnPlayerDeath.Broadcast();
	}

	NotifyChanged();
}

void UEclipseGameStateSubsystem::ChangeHeat(int32 Delta)        { ChangeMeter(TEXT("heat"),        Delta); }
void UEclipseGameStateSubsystem::ChangeThirst(int32 Delta)      { ChangeMeter(TEXT("thirst"),      Delta); }
void UEclipseGameStateSubsystem::ChangeStimulation(int32 Delta) { ChangeMeter(TEXT("stimulation"), Delta); }

void UEclipseGameStateSubsystem::TickChapterClock(float DeltaSeconds)
{
	// Plain accumulator — no auto-advance. Other systems (NPC movement
	// schedules, ambient cues) read ChapterElapsedSeconds to drive their
	// own behaviour. Chapter advances are manual: triggered by quest /
	// dialogue beats via OnChapterTransition() or SkipChapter() (debug).
	//
	// ClockScale converts wall-clock to game-clock — default 2.0 means
	// 30 real seconds reads as 1:00 of in-game time on the HUD readout.
	if (!bClockRunning || DeltaSeconds <= 0.f) return;
	ChapterElapsedSeconds += DeltaSeconds * ClockScale;
}

float UEclipseGameStateSubsystem::GetChapterDurationSeconds() const
{
	if (ChapterTable.IsValidIndex(Chapter) && ChapterTable[Chapter])
	{
		const float D = ChapterTable[Chapter]->DurationSeconds;
		if (D > 0.f) return D;
	}
	return FMath::Max(1.f, DefaultChapterDurationSeconds);
}

FText UEclipseGameStateSubsystem::GetChapterTitle() const
{
	if (ChapterTable.IsValidIndex(Chapter) && ChapterTable[Chapter])
	{
		const FText& Name = ChapterTable[Chapter]->DisplayName;
		if (!Name.IsEmpty()) return Name;
	}
	return FText::FromString(FString::Printf(TEXT("Chapter %d"), Chapter));
}

FText UEclipseGameStateSubsystem::GetChapterClockText() const
{
	// "H:MM" 24-hour, derived from ChapterElapsedSeconds (chapter clock
	// starts at 0:00 and counts up — the same accumulator the dialogue
	// adds +20s to on continuing clicks). Wraps every 24h so the display
	// reads as a real clock instead of drifting past midnight.
	const float Elapsed   = FMath::Max(0.f, ChapterElapsedSeconds);
	const int32 TotalMins = FMath::FloorToInt(Elapsed / 60.f);
	const int32 HourOfDay = (TotalMins / 60) % 24;
	const int32 MinOfHour = TotalMins % 60;
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), HourOfDay, MinOfHour));
}

FText UEclipseGameStateSubsystem::GetChapterLabelText() const
{
	return FText::FromString(FString::Printf(TEXT("CH %d"), Chapter));
}

void UEclipseGameStateSubsystem::SkipChapter()
{
	UE_LOG(LogEclipse, Log, TEXT("Chapter clock: manual chapter advance (debug)"));

	OnChapterTransition();                       // ++Chapter, reset per-chapter state, NotifyChanged
	ShowChapterCard(GetChapterTitle());          // fades the chapter card in/out via the existing widget
	OnChapterAdvanced.Broadcast(Chapter);        // v2+ hook (NPC shuffle, music swap, etc.)
	ChapterElapsedSeconds = 0.f;                 // restart the clock at 0 for the new chapter
}

// ── Debug console command: `Eclipse.SkipChapter` ──
// Walks every world looking for the GameInstance's GameStateSubsystem and
// fires SkipChapter on it. Lets us advance the clock from the in-editor
// console without having to wire a key binding.
static FAutoConsoleCommandWithWorld GSkipChapterCmd(
	TEXT("Eclipse.SkipChapter"),
	TEXT("Force-advance the chapter clock to the next chapter."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World) return;
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				GS->SkipChapter();
			}
		}
	})
);

void UEclipseGameStateSubsystem::OnChapterTransition()
{
	++Chapter;
	FailedChoicesThisChapter.Reset();
	UE_LOG(LogEclipse, Log, TEXT("Chapter transition → %d"), Chapter);
	NotifyChanged();
}

bool UEclipseGameStateSubsystem::HasMetNPC(FName Name) const
{
	return MetNPCs.ContainsByPredicate([&](const FEclipseMetNpc& M){ return M.Name == Name; });
}

void UEclipseGameStateSubsystem::RecordMetNPC(FName Name, FName DialogueId)
{
	if (HasMetNPC(Name)) return;
	FEclipseMetNpc M; M.Name = Name; M.DialogueId = DialogueId;
	MetNPCs.Add(M);
	NotifyChanged();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Save / Load — autosave at "ECLIPSE_AUTOSAVE" + manual slots
//  ("ECLIPSE_SLOT_0/1/2"). Both reuse the snapshot helpers below; the only
//  difference is the slot string.
//
//  Triggered by UEclipseGameInstance::Init (load) and ::Shutdown (save). The
//  save object mirrors the serializable fields on this subsystem.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Build the SaveGame from current subsystem + world state.
	UEclipseSaveGame* CreateSnapshot(UEclipseGameStateSubsystem& GS, UWorld* W)
	{
		UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UEclipseSaveGame::StaticClass()));
		if (!Save) return nullptr;

		Save->Aesthetics               = GS.Aesthetics;
		Save->Rhythm                   = GS.Rhythm;
		Save->Zen                      = GS.Zen;
		Save->Psychedelics             = GS.Psychedelics;
		Save->SelectedCharacterId      = GS.SelectedCharacterId;
		Save->Annoyance                = GS.Annoyance;
		Save->Heat                     = GS.Heat;
		Save->Thirst                   = GS.Thirst;
		Save->Stimulation              = GS.Stimulation;
		Save->Inventory                = GS.Inventory;
		Save->EquippedClothing         = GS.EquippedClothing;
		Save->EquippedSlots            = GS.EquippedSlots;
		Save->Tokens                   = GS.Tokens;
		Save->bHasWristband            = GS.bHasWristband;
		Save->Coins                    = GS.Coins;
		Save->Notes                    = GS.Notes;
		Save->ItemSlotPositions        = GS.ItemSlotPositions;
		Save->Quest                    = GS.Quest;
		Save->MetNPCs                  = GS.MetNPCs;
		Save->FailedChoicesThisChapter = GS.FailedChoicesThisChapter;
		Save->bVipAccessGranted        = GS.bVipAccessGranted;
		Save->Chapter                  = GS.Chapter;
		Save->ChapterElapsedSeconds    = GS.ChapterElapsedSeconds;
		Save->SavedAt                  = FDateTime::Now();

		if (W)
		{
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				AActor* A = *It;
				if (A && A->GetClass()->GetName().Contains(TEXT("EclipseBaseRoom")))
				{
					if (FNameProperty* P = FindFProperty<FNameProperty>(A->GetClass(), TEXT("RoomKey")))
					{
						Save->CurrentLevelKey = *P->ContainerPtrToValuePtr<FName>(A);
					}
					if (FTextProperty* P = FindFProperty<FTextProperty>(A->GetClass(), TEXT("DisplayName")))
					{
						Save->RoomDisplayName = P->ContainerPtrToValuePtr<FText>(A)->ToString();
					}
					break;
				}
			}
			if (APlayerController* PC = W->GetFirstPlayerController())
			{
				if (APawn* Pawn = PC->GetPawn())
				{
					Save->PlayerWorldLocation = Pawn->GetActorLocation();
					Save->PlayerWorldRotation = Pawn->GetActorRotation();
				}
			}
		}
		return Save;
	}

	// Restore subsystem state from a SaveGame. bImmediateTeleport tells the
	// caller whether the player got moved now or needs a pending-teleport queue.
	void ApplySnapshot(UEclipseGameStateSubsystem& GS, UEclipseSaveGame* Save,
		UWorld* W, bool& bImmediateTeleport)
	{
		bImmediateTeleport = false;
		GS.Aesthetics               = Save->Aesthetics;
		GS.Rhythm                   = Save->Rhythm;
		GS.Zen                      = Save->Zen;
		GS.Psychedelics             = Save->Psychedelics;

		// Re-derive Gender/Race from the saved character, then restore the
		// dynamic Annoyance value (SelectCharacter seeds it from
		// StartingAnnoyance; the saved runtime value overrides that). If the
		// character id is empty/unknown (legacy save), leave identity at
		// defaults and just restore Annoyance.
		GS.SelectCharacter(Save->SelectedCharacterId);
		GS.SelectedCharacterId = Save->SelectedCharacterId;
		GS.Annoyance           = FMath::Clamp(Save->Annoyance, 0, UEclipseGameStateSubsystem::AnnoyanceMax);

		// Meter migration: old saves stored these as floats on a 0..100
		// scale. The Save struct's fields are now int32 but auto-load may
		// have read pre-refactor data that overflowed >10 (e.g. 80 for
		// "thirst quenched"). Detect any value > MeterMax and rescale.
		auto MigrateMeter = [](int32 V) -> int32
		{
			if (V <= UEclipseGameStateSubsystem::MeterMax) return FMath::Clamp(V, 0, UEclipseGameStateSubsystem::MeterMax);
			// Old float-scale value packed into the int — round down by 10.
			return FMath::Clamp(V / 10, 0, UEclipseGameStateSubsystem::MeterMax);
		};
		GS.Heat                     = MigrateMeter(Save->Heat);
		GS.Thirst                   = MigrateMeter(Save->Thirst);
		GS.Stimulation              = MigrateMeter(Save->Stimulation);
		GS.Inventory                = Save->Inventory;
		GS.EquippedClothing         = Save->EquippedClothing;
		GS.EquippedSlots            = Save->EquippedSlots;
		GS.Tokens                   = Save->Tokens;
		GS.bHasWristband            = Save->bHasWristband;
		GS.Coins                    = Save->Coins;
		GS.Notes                    = Save->Notes;
		GS.ItemSlotPositions        = Save->ItemSlotPositions;
		GS.Quest                    = Save->Quest;
		GS.MetNPCs                  = Save->MetNPCs;
		GS.FailedChoicesThisChapter = Save->FailedChoicesThisChapter;
		GS.bVipAccessGranted        = Save->bVipAccessGranted;
		GS.Chapter                  = Save->Chapter;
		GS.ChapterElapsedSeconds    = Save->ChapterElapsedSeconds;

		// Saves from before the 6-slot wearables system don't carry an
		// EquippedSlots map. Re-apply baseline outfit so old saves get
		// the same fresh-game wardrobe instead of an empty paperdoll.
		GS.ApplyDefaultOutfitIfEmpty();

		if (W)
		{
			FName CurrentRoom;
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				AActor* A = *It;
				if (A && A->GetClass()->GetName().Contains(TEXT("EclipseBaseRoom")))
				{
					if (FNameProperty* P = FindFProperty<FNameProperty>(A->GetClass(), TEXT("RoomKey")))
					{
						CurrentRoom = *P->ContainerPtrToValuePtr<FName>(A);
						break;
					}
				}
			}
			if (CurrentRoom == Save->CurrentLevelKey)
			{
				if (APlayerController* PC = W->GetFirstPlayerController())
				{
					if (APawn* Pawn = PC->GetPawn())
					{
						Pawn->SetActorLocationAndRotation(Save->PlayerWorldLocation,
							Save->PlayerWorldRotation, false, nullptr, ETeleportType::TeleportPhysics);
						PC->SetControlRotation(Save->PlayerWorldRotation);
						bImmediateTeleport = true;
					}
				}
			}
		}
	}
}

bool UEclipseGameStateSubsystem::SaveCurrent()
{
	UEclipseSaveGame* Save = CreateSnapshot(*this, GetWorld());
	if (!Save) return false;

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save,
		UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex);
	UE_LOG(LogEclipse, Log,
		TEXT("SaveCurrent → %s (Room=%s Loc=%s)"),
		bOk ? TEXT("OK") : TEXT("FAILED"),
		*Save->CurrentLevelKey.ToString(), *Save->PlayerWorldLocation.ToString());
	return bOk;
}

bool UEclipseGameStateSubsystem::TryLoadCurrent()
{
	if (!UGameplayStatics::DoesSaveGameExist(
			UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex))
	{
		UE_LOG(LogEclipse, Log, TEXT("TryLoadCurrent: no save slot, fresh start"));
		return false;
	}

	UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
		UGameplayStatics::LoadGameFromSlot(
			UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex));
	if (!Save)
	{
		UE_LOG(LogEclipse, Warning, TEXT("TryLoadCurrent: slot exists but failed to load"));
		return false;
	}

	bool bImmediate = false;
	ApplySnapshot(*this, Save, GetWorld(), bImmediate);
	if (!bImmediate)
	{
		bPendingTeleport         = true;
		PendingTeleportLocation  = Save->PlayerWorldLocation;
		PendingTeleportRotation  = Save->PlayerWorldRotation;
	}
	NotifyChanged();
	UE_LOG(LogEclipse, Log, TEXT("TryLoadCurrent: restored Room=%s teleported=%s"),
		*Save->CurrentLevelKey.ToString(), bImmediate ? TEXT("now") : TEXT("pending"));
	return true;
}

bool UEclipseGameStateSubsystem::SaveToSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= UEclipseSaveGame::NumManualSlots)
	{
		UE_LOG(LogEclipse, Warning, TEXT("SaveToSlot: invalid slot %d"), SlotIndex);
		return false;
	}
	UEclipseSaveGame* Save = CreateSnapshot(*this, GetWorld());
	if (!Save) return false;

	const FString SlotName = UEclipseSaveGame::ManualSlotName(SlotIndex);
	const bool bOk = UGameplayStatics::SaveGameToSlot(Save, SlotName, UEclipseSaveGame::UserIndex);
	UE_LOG(LogEclipse, Log, TEXT("SaveToSlot[%d] -> %s (Room=%s Loc=%s)"),
		SlotIndex, bOk ? TEXT("OK") : TEXT("FAILED"),
		*Save->CurrentLevelKey.ToString(), *Save->PlayerWorldLocation.ToString());
	return bOk;
}

bool UEclipseGameStateSubsystem::LoadFromSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= UEclipseSaveGame::NumManualSlots) return false;
	const FString SlotName = UEclipseSaveGame::ManualSlotName(SlotIndex);
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UEclipseSaveGame::UserIndex))
	{
		UE_LOG(LogEclipse, Log, TEXT("LoadFromSlot[%d]: empty"), SlotIndex);
		return false;
	}
	UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName, UEclipseSaveGame::UserIndex));
	if (!Save) return false;

	bool bImmediate = false;
	ApplySnapshot(*this, Save, GetWorld(), bImmediate);
	if (!bImmediate)
	{
		bPendingTeleport         = true;
		PendingTeleportLocation  = Save->PlayerWorldLocation;
		PendingTeleportRotation  = Save->PlayerWorldRotation;
	}
	NotifyChanged();
	UE_LOG(LogEclipse, Log, TEXT("LoadFromSlot[%d] -> OK (teleported=%s)"),
		SlotIndex, bImmediate ? TEXT("now") : TEXT("pending"));
	return true;
}

FEclipseSaveSlotInfo UEclipseGameStateSubsystem::GetSlotInfo(int32 SlotIndex) const
{
	FEclipseSaveSlotInfo Info;
	Info.SlotIndex = SlotIndex;
	if (SlotIndex < 0 || SlotIndex >= UEclipseSaveGame::NumManualSlots) return Info;

	const FString SlotName = UEclipseSaveGame::ManualSlotName(SlotIndex);
	if (!UGameplayStatics::DoesSaveGameExist(SlotName, UEclipseSaveGame::UserIndex))
	{
		Info.DisplayLabel = FString::Printf(TEXT("SLOT %d  ·  EMPTY"), SlotIndex + 1);
		return Info;
	}

	UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName, UEclipseSaveGame::UserIndex));
	if (!Save) return Info;

	Info.bExists           = true;
	Info.RoomDisplayName   = Save->RoomDisplayName.IsEmpty() ? Save->CurrentLevelKey.ToString() : Save->RoomDisplayName;
	Info.CurrentLevelKey   = Save->CurrentLevelKey;
	Info.Chapter           = Save->Chapter;
	Info.SavedAt           = Save->SavedAt;

	const FString TimeStr = Save->SavedAt.ToString(TEXT("%Y-%m-%d %H:%M"));
	Info.DisplayLabel = FString::Printf(TEXT("SLOT %d  ·  %s  ·  CH %d  ·  %s"),
		SlotIndex + 1,
		Info.RoomDisplayName.IsEmpty() ? TEXT("?") : *Info.RoomDisplayName,
		Info.Chapter,
		*TimeStr);
	return Info;
}

bool UEclipseGameStateSubsystem::DeleteSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= UEclipseSaveGame::NumManualSlots) return false;
	const FString SlotName = UEclipseSaveGame::ManualSlotName(SlotIndex);
	const bool bOk = UGameplayStatics::DeleteGameInSlot(SlotName, UEclipseSaveGame::UserIndex);
	UE_LOG(LogEclipse, Log, TEXT("DeleteSlot[%d] -> %s"), SlotIndex, bOk ? TEXT("OK") : TEXT("FAILED"));
	return bOk;
}

void UEclipseGameStateSubsystem::ConsumePendingTeleport(APawn* Pawn)
{
	if (!bPendingTeleport || !Pawn) return;

	Pawn->SetActorLocationAndRotation(PendingTeleportLocation, PendingTeleportRotation,
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = Pawn->GetController())
	{
		C->SetControlRotation(PendingTeleportRotation);
	}

	UE_LOG(LogEclipse, Log,
		TEXT("ConsumePendingTeleport: pawn %s -> Loc=%s Rot=%s"),
		*Pawn->GetName(), *PendingTeleportLocation.ToString(), *PendingTeleportRotation.ToString());

	bPendingTeleport = false;
	PendingTeleportLocation = FVector::ZeroVector;
	PendingTeleportRotation = FRotator::ZeroRotator;
}
