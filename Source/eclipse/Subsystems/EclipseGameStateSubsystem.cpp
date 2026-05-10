// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameStateSubsystem.h"
#include "Eclipse.h"
#include "Save/EclipseSaveGame.h"
#include "Data/EclipseChapterDefinition.h"
#include "Data/EclipseItemDefinition.h"
#include "Data/EclipseClothingDefinition.h"
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
		ClothingTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Justin/Data/DT_Clothing.DT_Clothing"));
		UE_LOG(LogEclipse, Log, TEXT("ClothingTable auto-load %s"),
			ClothingTable ? TEXT("OK") : TEXT("not present (designer can author later)"));
	}
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
	Inventory.Add(ItemId);
	if (ItemId == TEXT("wristband")) bHasWristband = true;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::RemoveItem(FName ItemId)
{
	const int32 Idx = Inventory.IndexOfByKey(ItemId);
	if (Idx == INDEX_NONE) return false;
	Inventory.RemoveAt(Idx);
	ItemSlotPositions.Remove(ItemId);   // free the grid slot
	if (ItemId == TEXT("wristband")) bHasWristband = false;
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

		// Usable: restores thirst by Row.Effect.RestoreThirst (per-item tuned
		// in DT_Items). RestoreThirst <= 0 means "empty container, can't be
		// consumed" — refuse the use so empty baggies / glasses don't vanish
		// into nothing when the player clicks USE on them.
		// (Other Effect fields like HeatGainMult / CoolRate are equip-time
		// modifiers, applied while an Equippable item is worn — wired up
		// in a future milestone.)
		if (Row.Type == EEclipseItemType::Usable)
		{
			if (Row.Effect.RestoreThirst <= 0.f)
			{
				UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' refused — Usable but no effect (empty container)"),
					*ItemId.ToString());
				return false;
			}
			Thirst = FMath::Clamp(Thirst + Row.Effect.RestoreThirst, 0.f, MaxThirst);
		}

		UE_LOG(LogEclipse, Log, TEXT("UseItem '%s' (type=%d quest='%s')"),
			*ItemId.ToString(), (int32)Row.Type, *Row.QuestFlag.ToString());
	}

	return RemoveItem(ItemId);
}

bool UEclipseGameStateSubsystem::GetItemRow(FName ItemId, FEclipseItemRow& OutRow) const
{
	if (!ItemTable) return false;
	const FEclipseItemRow* Found = ItemTable->FindRow<FEclipseItemRow>(ItemId, TEXT("InventoryUI"));
	if (!Found) return false;
	OutRow = *Found;
	return true;
}

bool UEclipseGameStateSubsystem::GetClothingRow(FName ClothingId, FEclipseClothingRow& OutRow) const
{
	if (!ClothingTable) return false;
	const FEclipseClothingRow* Found = ClothingTable->FindRow<FEclipseClothingRow>(ClothingId, TEXT("InventoryUI"));
	if (!Found) return false;
	OutRow = *Found;
	return true;
}

void UEclipseGameStateSubsystem::DrainThirst(float Amount)
{
	Thirst = FMath::Clamp(Thirst - Amount, 0.f, MaxThirst);
	NotifyChanged();
}

void UEclipseGameStateSubsystem::TickMeters(float DeltaSeconds)
{
	if (DeltaSeconds <= 0.f) return;

	// Drain both meters. Allow values to dip below zero internally for
	// fractional accuracy then clamp; the bars use the clamped value.
	Thirst = FMath::Max(0.f, Thirst - ThirstDrainPerSec * DeltaSeconds);
	Heat   = FMath::Max(0.f, Heat   - HeatDrainPerSec   * DeltaSeconds);

	// Tick the chapter clock alongside the meters — same pause / dialogue
	// gating, since the player character's TickMeters call site already
	// guards both with bDialogueOpen / world-paused checks.
	TickChapterClock(DeltaSeconds);

	// Throttle delegate broadcasts to ~1Hz so the HUD doesn't re-bind every
	// frame. The bar SetPercent inside UpdateBars is cheap, but UMG layout
	// invalidation still has cost.
	MetersBroadcastAccum += DeltaSeconds;
	if (MetersBroadcastAccum >= 1.0f)
	{
		MetersBroadcastAccum = 0.f;
		NotifyChanged();
	}
}

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

void UEclipseGameStateSubsystem::GainHeat(float Amount)
{
	Heat = FMath::Clamp(Heat + Amount, 0.f, MaxHeat);
	NotifyChanged();
}

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

		Save->Word                     = GS.Word;
		Save->Rhythm                   = GS.Rhythm;
		Save->Shadow                   = GS.Shadow;
		Save->Heat                     = GS.Heat;
		Save->Thirst                   = GS.Thirst;
		Save->Inventory                = GS.Inventory;
		Save->EquippedClothing         = GS.EquippedClothing;
		Save->Tokens                   = GS.Tokens;
		Save->bHasWristband            = GS.bHasWristband;
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
		GS.Word                     = Save->Word;
		GS.Rhythm                   = Save->Rhythm;
		GS.Shadow                   = Save->Shadow;
		GS.Heat                     = Save->Heat;
		GS.Thirst                   = Save->Thirst;
		GS.Inventory                = Save->Inventory;
		GS.EquippedClothing         = Save->EquippedClothing;
		GS.Tokens                   = Save->Tokens;
		GS.bHasWristband            = Save->bHasWristband;
		GS.ItemSlotPositions        = Save->ItemSlotPositions;
		GS.Quest                    = Save->Quest;
		GS.MetNPCs                  = Save->MetNPCs;
		GS.FailedChoicesThisChapter = Save->FailedChoicesThisChapter;
		GS.bVipAccessGranted        = Save->bVipAccessGranted;
		GS.Chapter                  = Save->Chapter;
		GS.ChapterElapsedSeconds    = Save->ChapterElapsedSeconds;

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
