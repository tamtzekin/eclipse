// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseItemDefinition.h" // FEclipseItemEffect
#include "EclipseClothingDefinition.generated.h"

UENUM(BlueprintType)
enum class EEclipseSlotType : uint8
{
	Head      UMETA(DisplayName = "Head"),
	Eyes      UMETA(DisplayName = "Eyes"),
	Neck      UMETA(DisplayName = "Neck"),
	Top       UMETA(DisplayName = "Top"),
	Bottom    UMETA(DisplayName = "Bottom"),
	Shoes     UMETA(DisplayName = "Shoes"),
};

// Aliases for code that still references the old names — Jacket → Top
// is the spiritual successor since both occupy the torso. Keep these
// here for one revision so DT rows authored against the old enum keep
// loading.
//#define EEclipseSlotType_Jacket EEclipseSlotType::Top

USTRUCT(BlueprintType)
struct FEclipseClothingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Icon;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Description;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor TintColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FEclipseItemEffect Effect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 TokenNum = 0;          // ticket number — matches Cloak Check claims
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EEclipseSlotType SlotType = EEclipseSlotType::Head;
};
