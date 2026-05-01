// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseItemDefinition.generated.h"

UENUM(BlueprintType)
enum class EEclipseItemType : uint8
{
	Consumable, Held, Special, Quest
};

USTRUCT(BlueprintType)
struct FEclipseItemEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) float SpeedMult        = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CoolRate         = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float HeatGainMult     = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ThirstDrainMult  = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bMotionBlur      = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bRevealNPCs      = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bDarken          = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bCharisma        = false;
};

USTRUCT(BlueprintType)
struct FEclipseItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Icon;            // emoji or short string
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor TintColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EEclipseItemType Type = EEclipseItemType::Held;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FEclipseItemEffect Effect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Duration = 0.f;    // 0 = permanent / non-timed
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName StatBoost;         // "word"|"rhythm"|"shadow"|None
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName QuestFlag;         // e.g. "hasHair" — for Angel's Hair drink
};
