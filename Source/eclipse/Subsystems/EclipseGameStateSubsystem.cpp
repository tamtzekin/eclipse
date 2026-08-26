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
#include "Items/EclipseItemActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

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

	// (There used to be a "test wardrobe" here that pushed seven unequipped
	// garments into Inventory to give the old AVAILABLE grid chips to drag.
	// That grid is gone, and the carry limit is three, so spare clothes now
	// have to be found in the world, worn, or left behind.)
	UE_LOG(LogEclipse, Log, TEXT("GS: applied default outfit (empty slot map)"));
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

	// Everything carried must live in a carrier — there's no backpack to
	// fall back on. If we can't find room even after dropping what's in
	// hand, undo the pickup so the item stays in the world rather than
	// becoming an invisible orphan.
	if (!AutoPlace(UniqueId))
	{
		Inventory.Remove(UniqueId);
		UE_LOG(LogEclipse, Log, TEXT("AddItem: '%s' refused — nowhere to put it"), *UniqueId.ToString());
		return false;
	}

	// Quest flag check uses the base id so a runtime id like
	// "wristband__Item_Wristband" still flips bHasWristband on pickup.
	if (GetBaseItemId(UniqueId) == TEXT("wristband")) bHasWristband = true;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::AutoPlace(FName ItemId)
{
	// Pockets first for small things, so the hands stay free for whatever
	// you pick up next.
	if (CanPlaceInSlot(ItemId, EEclipseSlotType::Pockets))
	{
		ItemPlacements.Add(ItemId, EEclipseSlotType::Pockets);
		return true;
	}
	if (CanPlaceInSlot(ItemId, EEclipseSlotType::Hands))
	{
		ItemPlacements.Add(ItemId, EEclipseSlotType::Hands);
		return true;
	}

	// Full. This used to quietly drop whatever was in your hands and take
	// the new thing regardless — you'd walk past a pickup and lose what you
	// were carrying without ever agreeing to it. Now it just refuses, and
	// the caller (AEclipseItemActor::Pickup) offers an explicit swap.
	return false;
}

TArray<FName> UEclipseGameStateSubsystem::GetSwapCandidates(FName IncomingId) const
{
	TArray<FName> Out;

	// Anything Large needs the hands, so only what's in the hands can free
	// up room for it. Something Small could go in either, so everything
	// carried is a fair trade.
	const bool bNeedsHands = (GetItemSize(IncomingId) == EEclipseItemSize::Large);

	for (const FName& Held : GetItemsInSlot(EEclipseSlotType::Hands))
	{
		if (Held != IncomingId) Out.Add(Held);
	}
	if (!bNeedsHands)
	{
		for (const FName& Held : GetItemsInSlot(EEclipseSlotType::Pockets))
		{
			if (Held != IncomingId) Out.Add(Held);
		}
	}
	return Out;
}

bool UEclipseGameStateSubsystem::SwapCarriedItem(FName OutgoingId, FName IncomingId)
{
	if (!Inventory.Contains(OutgoingId)) return false;

	// Remember where the outgoing item sat: if the incoming one turns out
	// not to fit after all, we've already put the old one on the floor and
	// the player would be left with neither. Checking first is cheaper than
	// unwinding a spawned pickup actor.
	const EEclipseSlotType* Carrier = ItemPlacements.Find(OutgoingId);
	if (!Carrier) return false;

	const bool bIncomingFits =
		(*Carrier == EEclipseSlotType::Hands) ||
		(GetItemSize(IncomingId) != EEclipseItemSize::Large);
	if (!bIncomingFits)
	{
		UE_LOG(LogEclipse, Log, TEXT("Swap refused: '%s' is too big for the %s slot '%s' frees up"),
			*IncomingId.ToString(),
			(*Carrier == EEclipseSlotType::Hands) ? TEXT("hands") : TEXT("pocket"),
			*OutgoingId.ToString());
		return false;
	}

	if (!DropItemToWorld(OutgoingId)) return false;   // no pawn — keep holding it

	if (!AddItem(IncomingId))
	{
		UE_LOG(LogEclipse, Warning,
			TEXT("Swap: dropped '%s' but couldn't take '%s' — both are now in the room"),
			*OutgoingId.ToString(), *IncomingId.ToString());
		return false;
	}

	UE_LOG(LogEclipse, Log, TEXT("Swapped '%s' for '%s'"), *OutgoingId.ToString(), *IncomingId.ToString());
	return true;
}

FText UEclipseGameStateSubsystem::GetItemDisplayName(FName ItemId) const
{
	FEclipseItemRow IRow;
	if (GetItemRow(ItemId, IRow) && !IRow.DisplayName.IsEmpty()) return IRow.DisplayName;

	FEclipseClothingRow CRow;
	if (GetClothingRow(ItemId, CRow) && !CRow.DisplayName.IsEmpty()) return CRow.DisplayName;

	// No row (or a row with no wording yet) — show the base id in caps so
	// the prompt still names something the player can recognise on the
	// floor, rather than going blank.
	return FText::FromString(GetBaseItemId(ItemId).ToString().Replace(TEXT("_"), TEXT(" ")).ToUpper());
}

bool UEclipseGameStateSubsystem::MoveItemToCarrier(FName ItemId, EEclipseSlotType Carrier)
{
	if (!CanPlaceInSlot(ItemId, Carrier)) return false;

	// Coming off the body: clear both the flat list and the slot map, or the
	// garment would show as worn AND carried.
	if (EquippedClothing.Contains(ItemId))
	{
		EquippedClothing.Remove(ItemId);
		for (auto It = EquippedSlots.CreateIterator(); It; ++It)
		{
			if (It.Value() == ItemId) It.RemoveCurrent();
		}
	}
	if (!Inventory.Contains(ItemId)) Inventory.Add(ItemId);

	ItemPlacements.Add(ItemId, Carrier);
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::ReturnToCarryOrDrop(FName ItemId)
{
	if (!Inventory.Contains(ItemId)) Inventory.Add(ItemId);
	if (AutoPlace(ItemId)) return true;

	// Hands and pockets are both full and we couldn't free the hands, so the
	// item goes on the floor. Never leave it carried-but-unplaced: with no
	// backpack in this design, that's the same as deleting it.
	UE_LOG(LogEclipse, Log, TEXT("ReturnToCarryOrDrop: no room for '%s' — dropping it"),
		*ItemId.ToString());
	DropItemToWorld(ItemId);
	return false;
}

void UEclipseGameStateSubsystem::MigrateCarryPlacements()
{
	// Drop stale entries first: anything placed that isn't carried any more,
	// and anything whose recorded carrier isn't a carrier at all (a save
	// written when this map held 0..17 grid indices deserialises its ints
	// into arbitrary enum values).
	for (auto It = ItemPlacements.CreateIterator(); It; ++It)
	{
		const bool bStillHeld = Inventory.Contains(It.Key());
		const bool bRealSlot  = It.Value() == EEclipseSlotType::Hands
		                     || It.Value() == EEclipseSlotType::Pockets;
		if (!bStillHeld || !bRealSlot) It.RemoveCurrent();
	}

	// Re-seat anything unplaced, in inventory order so the player's first
	// items win the space. Overflow goes on the floor rather than being
	// deleted — losing a quest item to a silent migration is unforgivable.
	TArray<FName> Overflow;
	for (const FName& Id : Inventory)
	{
		if (ItemPlacements.Contains(Id)) continue;

		if (CanPlaceInSlot(Id, EEclipseSlotType::Pockets))
		{
			ItemPlacements.Add(Id, EEclipseSlotType::Pockets);
		}
		else if (CanPlaceInSlot(Id, EEclipseSlotType::Hands))
		{
			ItemPlacements.Add(Id, EEclipseSlotType::Hands);
		}
		else
		{
			Overflow.Add(Id);
		}
	}

	for (const FName& Id : Overflow)
	{
		if (!DropItemToWorld(Id))
		{
			// No pawn yet (cross-level load). Leave it carried-but-unplaced;
			// this runs again from the inventory panel, by which point there
			// is a pawn to drop it in front of.
			UE_LOG(LogEclipse, Warning,
				TEXT("MigrateCarryPlacements: '%s' has no room and can't be dropped yet"),
				*Id.ToString());
		}
	}

	if (Overflow.Num() > 0)
	{
		UE_LOG(LogEclipse, Log, TEXT("MigrateCarryPlacements: %d item(s) overflowed the carry limit"),
			Overflow.Num());
	}
}

bool UEclipseGameStateSubsystem::RemoveItem(FName ItemId)
{
	const int32 Idx = Inventory.IndexOfByKey(ItemId);
	if (Idx == INDEX_NONE) return false;
	Inventory.RemoveAt(Idx);
	ItemPlacements.Remove(ItemId);   // no longer carried anywhere
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
		ItemPlacements.Remove(ClothingId);   // worn now, not carried
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
			const FName Displaced = *Existing;
			EquippedClothing.Remove(Displaced);
			// Same rule as UnequipSlot: the garment you just took off needs a
			// hand, a pocket, or the floor.
			ReturnToCarryOrDrop(Displaced);
			UE_LOG(LogEclipse, Log,
				TEXT("EquipClothingToSlot: slot %d previously had '%s' -> stowed or dropped"),
				(int32)Slot, *Displaced.ToString());
		}
	}

	// Move ClothingId out of the inventory grid into the slot.
	Inventory.RemoveAt(InvIdx);
	ItemPlacements.Remove(ClothingId);   // worn now, not carried
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

	// Taking something off has to put it somewhere real — a hand, a pocket,
	// or the floor. Adding it straight to Inventory with no carrier would
	// make it vanish from the only screen that shows it.
	ReturnToCarryOrDrop(ClothingId);

	UE_LOG(LogEclipse, Log,
		TEXT("UnequipSlot: slot %d -> '%s' taken off"),
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

const FName UEclipseGameStateSubsystem::CigaretteItemId(TEXT("cigarettes"));

// Keep the carried chip in step with the count: present while you have any,
// gone once you don't. Called from both cigarette mutators so the two can't
// drift apart.
void UEclipseGameStateSubsystem::SyncCigaretteChip()
{
	const bool bHeld = Inventory.Contains(CigaretteItemId);
	if (Cigarettes > 0 && !bHeld)
	{
		// Straight into a pocket rather than through AddItem: a pack of
		// smokes shouldn't be refused for a full inventory when it merges
		// into a chip you may already be carrying, and it must never be
		// given a "__actor" runtime id — the id IS the stack key.
		Inventory.Add(CigaretteItemId);
		if (!AutoPlace(CigaretteItemId))
		{
			// Genuinely nowhere to put it. Keep the count (they're in your
			// hand, conceptually) but don't leave an unplaced entry, which
			// the carry model treats as corrupt.
			Inventory.Remove(CigaretteItemId);
		}
	}
	else if (Cigarettes <= 0 && bHeld)
	{
		Inventory.Remove(CigaretteItemId);
		ItemPlacements.Remove(CigaretteItemId);
	}
}

void UEclipseGameStateSubsystem::AddCigarettes(int32 Amount)
{
	if (Amount <= 0) return;
	Cigarettes += Amount;
	SyncCigaretteChip();
	UE_LOG(LogEclipse, Log, TEXT("Currency: +%d cigarettes → %d total"), Amount, Cigarettes);
	NotifyChanged();
}

bool UEclipseGameStateSubsystem::RemoveCigarettes(int32 Amount)
{
	if (Amount <= 0) return true;
	if (Cigarettes < Amount)
	{
		UE_LOG(LogEclipse, Log, TEXT("Currency: cannot spend %d cigarettes (have %d)"), Amount, Cigarettes);
		return false;
	}
	Cigarettes -= Amount;
	SyncCigaretteChip();
	UE_LOG(LogEclipse, Log, TEXT("Currency: -%d cigarettes → %d total"), Amount, Cigarettes);
	NotifyChanged();
	return true;
}

// ── Carry model ────────────────────────────────────────────────────────────
//
// The player carries very little on purpose: one thing in the hands, two
// small things in the pockets. Everything else is worn, on the floor, or in
// a locker. Capacity lives in this one function so the Bumbag (+3) is a
// case here rather than a parallel system.

int32 UEclipseGameStateSubsystem::GetSlotCapacity(EEclipseSlotType Slot) const
{
	switch (Slot)
	{
	case EEclipseSlotType::Hands:   return 1;
	case EEclipseSlotType::Pockets: return 2;
	// Worn clothing — one garment per body slot.
	default:                        return 1;
	}
}

EEclipseItemSize UEclipseGameStateSubsystem::GetItemSize(FName ItemId) const
{
	// An id can name either table; clothing wins only if DT_Items misses,
	// since a row present in both is authored as an item first.
	FEclipseItemRow IRow;
	if (GetItemRow(ItemId, IRow)) return IRow.Size;

	FEclipseClothingRow CRow;
	if (GetClothingRow(ItemId, CRow)) return CRow.Size;

	// Unknown id — assume it pockets, so a missing DT row can't make an
	// item uncarryable.
	return EEclipseItemSize::Small;
}

TArray<FName> UEclipseGameStateSubsystem::GetItemsInSlot(EEclipseSlotType Slot) const
{
	// Walk Inventory rather than the placement map so the result keeps
	// inventory order, which is what the UI draws left-to-right.
	TArray<FName> Out;
	for (const FName& Id : Inventory)
	{
		const EEclipseSlotType* Where = ItemPlacements.Find(Id);
		if (Where && *Where == Slot) Out.Add(Id);
	}
	return Out;
}

bool UEclipseGameStateSubsystem::CanPlaceInSlot(FName ItemId, EEclipseSlotType Slot) const
{
	if (ItemId.IsNone()) return false;

	// Only the two carriers hold loose items; body slots go through
	// EquipClothingToSlot instead.
	if (Slot != EEclipseSlotType::Hands && Slot != EEclipseSlotType::Pockets) return false;

	// A pocket won't take something Large.
	if (Slot == EEclipseSlotType::Pockets && GetItemSize(ItemId) == EEclipseItemSize::Large)
	{
		return false;
	}

	// Count occupants, discounting the item itself so "move it where it
	// already is" doesn't read as full.
	int32 Used = 0;
	for (const FName& Id : GetItemsInSlot(Slot))
	{
		if (Id != ItemId) ++Used;
	}
	return Used < GetSlotCapacity(Slot);
}

bool UEclipseGameStateSubsystem::PlaceItemInSlot(FName ItemId, EEclipseSlotType Slot)
{
	if (!Inventory.Contains(ItemId))    return false;
	if (!CanPlaceInSlot(ItemId, Slot))  return false;

	const EEclipseSlotType* Existing = ItemPlacements.Find(ItemId);
	if (Existing && *Existing == Slot) return true;   // already there

	ItemPlacements.Add(ItemId, Slot);
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::DropItemToWorld(FName ItemId)
{
	const bool bIsClothing = !Inventory.Contains(ItemId) && EquippedClothing.Contains(ItemId);
	if (!bIsClothing && !Inventory.Contains(ItemId)) return false;

	// Spawn a pickup where the player is standing so the item physically
	// re-enters the world and can be collected again. Cylinder + dark-blue
	// MIC placeholder, matching the loose items already scattered around the
	// levels; per-item meshes are a later polish pass.
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (World && Pawn)
	{
		const FVector SpawnLoc = Pawn->GetActorLocation()
			+ Pawn->GetActorForwardVector() * 80.f + FVector(0.f, 0.f, 30.f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AEclipseItemActor* Pickup = World->SpawnActor<AEclipseItemActor>(
				AEclipseItemActor::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params))
		{
			// BASE id on the actor: Pickup_Implementation mints a fresh
			// runtime id on re-collection, so handing it the old one would
			// double-suffix.
			Pickup->ItemId = GetBaseItemId(ItemId);

			// Row-driven mesh + scale (DT_Items). A swapped-out item has to
			// look like the thing you just gave up, or you can't find it
			// again on the floor.
			Pickup->ApplyMeshFromRow();

			// Only fall back to the placeholder cylinder when the row has no
			// mesh authored yet.
			if (Pickup->Mesh && !Pickup->Mesh->GetStaticMesh())
			{
				if (UStaticMesh* Cyl = Cast<UStaticMesh>(StaticLoadObject(
					UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))))
				{
					Pickup->Mesh->SetStaticMesh(Cyl);
				}
				if (UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(
					UMaterialInterface::StaticClass(), nullptr,
					TEXT("/Game/Justin/Materials/MI_ItemDarkBlue.MI_ItemDarkBlue"))))
				{
					Pickup->Mesh->SetMaterial(0, Mat);
				}
				Pickup->SetActorScale3D(FVector(0.30f, 0.30f, 0.50f));
			}
#if WITH_EDITOR
			Pickup->SetActorLabel(FString::Printf(TEXT("Item_%s_dropped"), *Pickup->ItemId.ToString()));
#endif
		}
	}
	else
	{
		// No pawn to drop at (loading before the level is up, headless
		// tests). Removing without spawning would silently destroy the item,
		// so refuse instead and let the caller keep holding it.
		UE_LOG(LogEclipse, Warning, TEXT("DropItemToWorld('%s'): no pawn — keeping the item"),
			*ItemId.ToString());
		return false;
	}

	UE_LOG(LogEclipse, Log, TEXT("Dropped '%s' to the world"), *ItemId.ToString());
	return bIsClothing ? UnequipClothing(ItemId) : RemoveItem(ItemId);
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
				(Row.Effect.HeatDelta   != 0) ||
				(Row.Effect.ThirstDelta != 0);
			const bool bHasLegacy = (Row.Effect.RestoreThirst > 0.f);
			// A permanent stat boost is an effect in its own right — a
			// perfume that moves no meters must not be refused below as an
			// empty container.
			const bool bHasStatBoost = !Row.StatBoost.IsNone() && Row.StatBoostLevels != 0;

			if (!bAnyDelta && !bHasLegacy && !bHasStatBoost)
			{
				UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' refused — Usable but no effect (empty container)"),
					*ItemId.ToString());
				return false;
			}

			if (bAnyDelta)
			{
				if (Row.Effect.HeatDelta   != 0) ChangeHeat  (Row.Effect.HeatDelta);
				if (Row.Effect.ThirstDelta != 0) ChangeThirst(Row.Effect.ThirstDelta);
			}
			else if (bHasLegacy)
			{
				// Legacy 0..100-scale value → +N hydration on the new
				// 0..10 scale. (Pre-refactor DT rows assumed "high thirst
				// = hydrated" which matches the new orientation.)
				const int32 LegacyDelta = FMath::Max(1, FMath::RoundToInt(Row.Effect.RestoreThirst / 10.f));
				ChangeThirst(LegacyDelta);
			}

			// Permanent: the levels land on the stat itself, so the gain
			// outlives the RemoveItem below.
			if (bHasStatBoost)
			{
				ApplyStatDelta(Row.StatBoost, Row.StatBoostLevels);
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

// ── Stat XP — learn-by-doing progression ───────────────────────────────────

int32 UEclipseGameStateSubsystem::GetStatXP(FName StatKey) const
{
	if (StatKey == TEXT("aesthetics"))   return AestheticsXP;
	if (StatKey == TEXT("rhythm"))       return RhythmXP;
	if (StatKey == TEXT("zen"))          return ZenXP;
	if (StatKey == TEXT("psychedelics")) return PsychedelicsXP;
	return 0;
}

void UEclipseGameStateSubsystem::GrantStatXP(FName StatKey, int32 Amount)
{
	if (Amount <= 0) return;

	int32* Stat = nullptr;
	int32* XP   = nullptr;
	if      (StatKey == TEXT("aesthetics"))   { Stat = &Aesthetics;   XP = &AestheticsXP; }
	else if (StatKey == TEXT("rhythm"))       { Stat = &Rhythm;       XP = &RhythmXP; }
	else if (StatKey == TEXT("zen"))          { Stat = &Zen;          XP = &ZenXP; }
	else if (StatKey == TEXT("psychedelics")) { Stat = &Psychedelics; XP = &PsychedelicsXP; }

	if (!Stat)
	{
		UE_LOG(LogEclipse, Warning, TEXT("GrantStatXP: unknown stat key '%s' (+%d XP ignored)"),
			*StatKey.ToString(), Amount);
		return;
	}

	*XP += Amount;
	int32 LevelUps = 0;
	while (*XP >= StatXPToLevel)
	{
		*XP -= StatXPToLevel;
		++(*Stat);
		++LevelUps;
	}

	if (LevelUps > 0)
	{
		UE_LOG(LogEclipse, Log, TEXT("GrantStatXP: %s +%d XP -> LEVEL UP x%d (now %d, %d/%d XP)"),
			*StatKey.ToString(), Amount, LevelUps, *Stat, *XP, StatXPToLevel);
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("GrantStatXP: %s +%d XP (%d/%d)"),
			*StatKey.ToString(), Amount, *XP, StatXPToLevel);
	}
	OnStatXPGranted.Broadcast(StatKey, Amount, *Stat, LevelUps > 0);
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

// ── Life meters (Heat / Thirst) ────────────────────────────────────────────
//
// Sweet-spot 0..10 model. Meters do NOT drain over time — they only move
// when consumables, dialogue effects, or other explicit events push them
// via ChangeMeter / ChangeXxx. Death triggers at Heat == 0 (frozen);
// Thirst extremes are dialogue-gates and HUD-tint cues, not killers.

int32 UEclipseGameStateSubsystem::GetMeterValue(FName MeterKey) const
{
	if (MeterKey == TEXT("heat"))        return Heat;
	if (MeterKey == TEXT("thirst"))      return Thirst;
	return 0;
}

void UEclipseGameStateSubsystem::ChangeMeter(FName MeterKey, int32 Delta)
{
	int32* Field = nullptr;
	if      (MeterKey == TEXT("heat"))        Field = &Heat;
	else if (MeterKey == TEXT("thirst"))      Field = &Thirst;

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

	// Death is single-shot on the Heat 0 transition — freezing out is the
	// fail state now that Stimulation is gone. No re-fire if the meter is
	// repeatedly pushed past zero while already at 0.
	if (Field == &Heat && Before > 0 && *Field == 0)
	{
		UE_LOG(LogEclipse, Log, TEXT("Heat reached 0 — player died"));
		OnPlayerDeath.Broadcast();
	}

	// Overheating costs you water: hitting max Heat drains 1 Thirst, ONCE.
	// The latch clears below when Heat drops back off max, so a later climb
	// back to 10 charges again — but sitting at 10 doesn't drain repeatedly.
	// Lives here rather than in AdvanceGameTime so it fires however Heat got
	// to max (item, Ink effect, time), not just via the clock.
	if (Field == &Heat)
	{
		if (*Field >= MeterMax && !bMaxHeatThirstPenaltyApplied)
		{
			bMaxHeatThirstPenaltyApplied = true;
			UE_LOG(LogEclipse, Log, TEXT("Heat hit max — draining 1 Thirst (one-shot)"));
			// Recurses one level into ChangeMeter for thirst; that call
			// can't come back here (Field would be &Thirst), so no loop.
			ChangeMeter(TEXT("thirst"), -1);
		}
		else if (*Field < MeterMax)
		{
			bMaxHeatThirstPenaltyApplied = false;
		}
	}

	NotifyChanged();
}

void UEclipseGameStateSubsystem::AdvanceGameTime(float Seconds)
{
	if (Seconds <= 0.f) return;
	ChapterElapsedSeconds += Seconds;

	// Bleed Heat once per whole HeatDecayIntervalMinutes crossed. Loops
	// rather than firing once so a big jump (a debug skip, a scripted
	// time-of-night change) applies every interval it passed through.
	const float IntervalSeconds = FMath::Max(1, HeatDecayIntervalMinutes) * 60.f;
	while (ChapterElapsedSeconds - LastHeatDecayAtSeconds >= IntervalSeconds)
	{
		LastHeatDecayAtSeconds += IntervalSeconds;
		// Routed through ChangeMeter so the Heat==0 death trigger and the
		// OnStateChanged broadcast still happen — the clock is what kills
		// you if you never warm back up.
		ChangeMeter(TEXT("heat"), -1);
	}

	// Same shape for Thirst on its own, slower interval. Separate accumulator
	// so the two never have to share a period.
	const float ThirstIntervalSeconds = FMath::Max(1, ThirstDecayIntervalMinutes) * 60.f;
	while (ChapterElapsedSeconds - LastThirstDecayAtSeconds >= ThirstIntervalSeconds)
	{
		LastThirstDecayAtSeconds += ThirstIntervalSeconds;
		ChangeMeter(TEXT("thirst"), -1);
	}

	NotifyChanged();
}

void UEclipseGameStateSubsystem::ChangeHeat(int32 Delta)        { ChangeMeter(TEXT("heat"),        Delta); }
void UEclipseGameStateSubsystem::ChangeThirst(int32 Delta)      { ChangeMeter(TEXT("thirst"),      Delta); }

void UEclipseGameStateSubsystem::TickChapterClock(float DeltaSeconds)
{
	// Deliberately does nothing: the clock does NOT advance with wall-clock
	// time. Game-time only moves when the player spends it — today that
	// means talking (UEclipseDialogueSubsystem::MakeChoice adds a fixed
	// bump per continuing choice), matching the JS prototype where standing
	// still costs you nothing. Other systems (NPC schedules, ambient cues)
	// still read ChapterElapsedSeconds; they just see it change in discrete
	// steps rather than continuously.
	//
	// Kept as a no-op rather than deleted so the existing per-frame call in
	// AEclipsePlayerCharacter::Tick stays harmless, and so re-enabling a
	// real-time mode later is a one-line change here.
	(void)DeltaSeconds;
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
	const float Elapsed  = FMath::Max(0.f, ChapterElapsedSeconds);
	int32 TotalMins      = FMath::FloorToInt(Elapsed / 60.f);
	// Quantise the DISPLAY only — ChapterElapsedSeconds keeps its exact
	// value for anything that reads it (schedules, saves). Flooring rather
	// than rounding means the clock never shows time the player hasn't
	// actually spent yet.
	const int32 Step = FMath::Max(1, ClockDisplayStepMinutes);
	TotalMins = (TotalMins / Step) * Step;

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
	// Rebase the Heat bleed with it — leaving the old marker behind would
	// make the next AdvanceGameTime think many intervals had elapsed.
	LastHeatDecayAtSeconds = 0.f;
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
		Save->AestheticsXP             = GS.AestheticsXP;
		Save->RhythmXP                 = GS.RhythmXP;
		Save->ZenXP                    = GS.ZenXP;
		Save->PsychedelicsXP           = GS.PsychedelicsXP;
		Save->SelectedCharacterId      = GS.SelectedCharacterId;
		Save->Annoyance                = GS.Annoyance;
		Save->Heat                     = GS.Heat;
		Save->Thirst                   = GS.Thirst;
		Save->Inventory                = GS.Inventory;
		Save->EquippedClothing         = GS.EquippedClothing;
		Save->EquippedSlots            = GS.EquippedSlots;
		Save->Tokens                   = GS.Tokens;
		Save->bHasWristband            = GS.bHasWristband;
		Save->Coins                    = GS.Coins;
		Save->Notes                    = GS.Notes;
		Save->Cigarettes               = GS.Cigarettes;
		Save->ItemPlacements           = GS.ItemPlacements;
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
		GS.AestheticsXP             = Save->AestheticsXP;
		GS.RhythmXP                 = Save->RhythmXP;
		GS.ZenXP                    = Save->ZenXP;
		GS.PsychedelicsXP           = Save->PsychedelicsXP;

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
		GS.Inventory                = Save->Inventory;
		GS.EquippedClothing         = Save->EquippedClothing;
		GS.EquippedSlots            = Save->EquippedSlots;
		GS.Tokens                   = Save->Tokens;
		GS.bHasWristband            = Save->bHasWristband;
		GS.Coins                    = Save->Coins;
		GS.Notes                    = Save->Notes;
		GS.Cigarettes               = Save->Cigarettes;
		GS.ItemPlacements           = Save->ItemPlacements;
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

		// Saves from before the carry model have no placements, and old ones
		// routinely hold far more than hands+pockets can take. Fit what fits,
		// drop the rest at the player's feet.
		GS.MigrateCarryPlacements();

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
