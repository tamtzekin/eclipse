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
	UPROPERTY() int32 Word = 1;
	UPROPERTY() int32 Rhythm = 1;
	UPROPERTY() int32 Shadow = 1;
	UPROPERTY() float Heat = 60.f;
	UPROPERTY() float Thirst = 80.f;
	UPROPERTY() TArray<FName> Inventory;
	UPROPERTY() TArray<FName> EquippedClothing;
	UPROPERTY() TArray<int32> Tokens;
	UPROPERTY() bool bHasWristband = false;
	UPROPERTY() FEclipseQuestState Quest;
	UPROPERTY() TArray<FEclipseMetNpc> MetNPCs;
	UPROPERTY() bool bVipAccessGranted = false;
	UPROPERTY() int32 Chapter = 0;

	// World state
	UPROPERTY() FName CurrentLevelKey;          // e.g. "Bathroom"
	UPROPERTY() FVector PlayerWorldLocation = FVector::ZeroVector;
	UPROPERTY() FRotator PlayerWorldRotation = FRotator::ZeroRotator;

	static constexpr const TCHAR* SlotName = TEXT("ECLIPSE_AUTOSAVE");
	static constexpr int32 UserIndex = 0;
};
