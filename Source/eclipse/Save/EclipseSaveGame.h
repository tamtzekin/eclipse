// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "EclipseSaveGame.generated.h"

/**
 * Single-slot autosave on quit. Mirrors UEclipseGameStateSubsystem's serializable
 * fields. Slot name "ECLIPSE_AUTOSAVE", user index 0.
 */
UCLASS()
class ECLIPSE_API UEclipseSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// Stats — 4-stat system after Stimulation moved into the meter system.
	UPROPERTY() int32 Aesthetics   = 1;
	UPROPERTY() int32 Rhythm       = 1;
	UPROPERTY() int32 Zen          = 1;
	UPROPERTY() int32 Psychedelics = 1;

	// Learn-by-doing XP toward each stat's next level (0..StatXPToLevel-1).
	// Legacy saves without these fields load as 0 — progress starts fresh,
	// stat levels themselves are unaffected.
	UPROPERTY() int32 AestheticsXP   = 0;
	UPROPERTY() int32 RhythmXP       = 0;
	UPROPERTY() int32 ZenXP          = 0;
	UPROPERTY() int32 PsychedelicsXP = 0;

	// Selected playable character (roster row in DT_Characters). Gender +
	// Race are PER-CHARACTER, so we persist only the character id and
	// re-derive Gender/Race from the def on load. Annoyance is dynamic
	// (moves during play) so its runtime value is saved separately and
	// re-applied after the character def initialises it.
	UPROPERTY() FName SelectedCharacterId;
	UPROPERTY() int32 Annoyance = 0;

	// Meters — sweet-spot 0..10 ints. Defaults match the subsystem
	// (Heat=3, Thirst=5, Stimulation=7). Old float-scale saves from prior
	// builds are migrated at load time in TryLoadCurrent / LoadFromSlot:
	// any value > 10 is divided by 10 and clamped to [0, 10].
	UPROPERTY() int32 Heat        = 3;
	UPROPERTY() int32 Thirst      = 5;
	UPROPERTY() int32 Stimulation = 7;
	UPROPERTY() TArray<FName> Inventory;
	UPROPERTY() TArray<FName> EquippedClothing;
	UPROPERTY() TMap<EEclipseSlotType, FName> EquippedSlots;
	UPROPERTY() TArray<int32> Tokens;
	UPROPERTY() bool bHasWristband = false;

	// Currency counters (separate from Inventory chips).
	UPROPERTY() int32 Coins = 0;
	UPROPERTY() int32 Notes = 0;

	// Per-item preferred slot inside the 6×3 inventory grid. Without this
	// the player's hand-arranged layout resets to top-left packing on load,
	// which feels wrong in a "your stuff is yours" RPG inventory.
	UPROPERTY() TMap<FName, int32> ItemSlotPositions;

	UPROPERTY() FEclipseQuestState Quest;
	UPROPERTY() TArray<FEclipseMetNpc> MetNPCs;

	// Choices the player attempted but failed (skill-check shortfalls etc.)
	// since the chapter started. Cleared on chapter advance, but mid-chapter
	// saves should still preserve them so reloading doesn't let the player
	// retry a failed roll.
	UPROPERTY() TSet<FName> FailedChoicesThisChapter;

	UPROPERTY() bool bVipAccessGranted = false;
	UPROPERTY() int32 Chapter = 0;
	UPROPERTY() float ChapterElapsedSeconds = 0.f;

	// World state
	UPROPERTY() FName CurrentLevelKey;          // e.g. "Bathroom"
	UPROPERTY() FVector PlayerWorldLocation = FVector::ZeroVector;
	UPROPERTY() FRotator PlayerWorldRotation = FRotator::ZeroRotator;

	// Metadata — populated on save, used by the pause menu's slot picker
	// to render "Slot 1 · Bathroom · Ch 1 · 2026-05-07 14:23" labels.
	UPROPERTY() FDateTime SavedAt = FDateTime(0);
	UPROPERTY() FString  RoomDisplayName;       // human label for CurrentLevelKey

	// Autosave slot (used by GameInstance::Init/Shutdown + dialogue startGame).
	static constexpr const TCHAR* SlotName = TEXT("ECLIPSE_AUTOSAVE");
	static constexpr int32 UserIndex = 0;

	// Manual slots — pause menu's "SAVE 1/2/3" / "LOAD 1/2/3" actions.
	static constexpr int32 NumManualSlots = 3;
	static FString ManualSlotName(int32 SlotIndex)
	{
		return FString::Printf(TEXT("ECLIPSE_SLOT_%d"), FMath::Clamp(SlotIndex, 0, NumManualSlots - 1));
	}
};

// FEclipseSaveSlotInfo is declared in EclipseGameStateSubsystem.h to avoid a
// circular include — that header is consumed by SaveGame, and the subsystem
// is the place the slot-info struct is returned from.
