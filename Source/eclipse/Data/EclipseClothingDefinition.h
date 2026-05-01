// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseItemDefinition.h" // FEclipseItemEffect
#include "EclipseClothingDefinition.generated.h"

UENUM(BlueprintType)
enum class EEclipseSlotType : uint8
{
	Head, Jacket, Neck
};

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
