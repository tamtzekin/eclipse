// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameStateSubsystem.h"
#include "Eclipse.h"
#include "Save/EclipseSaveGame.h"
#include "Data/EclipseChapterDefinition.h"
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
	if (N > 0) NotifyChanged();
	return N > 0;
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

		Save->Word              = GS.Word;
		Save->Rhythm            = GS.Rhythm;
		Save->Shadow            = GS.Shadow;
		Save->Heat              = GS.Heat;
		Save->Thirst            = GS.Thirst;
		Save->Inventory         = GS.Inventory;
		Save->EquippedClothing  = GS.EquippedClothing;
		Save->Tokens            = GS.Tokens;
		Save->bHasWristband     = GS.bHasWristband;
		Save->Quest             = GS.Quest;
		Save->MetNPCs           = GS.MetNPCs;
		Save->bVipAccessGranted = GS.bVipAccessGranted;
		Save->Chapter                = GS.Chapter;
		Save->ChapterElapsedSeconds  = GS.ChapterElapsedSeconds;
		Save->SavedAt           = FDateTime::Now();

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
		GS.Word              = Save->Word;
		GS.Rhythm            = Save->Rhythm;
		GS.Shadow            = Save->Shadow;
		GS.Heat              = Save->Heat;
		GS.Thirst            = Save->Thirst;
		GS.Inventory         = Save->Inventory;
		GS.EquippedClothing  = Save->EquippedClothing;
		GS.Tokens            = Save->Tokens;
		GS.bHasWristband     = Save->bHasWristband;
		GS.Quest             = Save->Quest;
		GS.MetNPCs           = Save->MetNPCs;
		GS.bVipAccessGranted = Save->bVipAccessGranted;
		GS.Chapter                = Save->Chapter;
		GS.ChapterElapsedSeconds  = Save->ChapterElapsedSeconds;

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
