// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "EclipseItemDefinition.h" // FEclipseItemEffect
#include "EclipseClothingDefinition.generated.h"

// Every place a thing can sit on the player. The first six are worn
// clothing (one item each, tracked in EquippedSlots). Hands and Pockets
// are CARRIERS — they hold loose items rather than outfit pieces, they
// have a capacity greater than one in the Pockets case, and they're
// tracked in ItemPlacements instead. See
// UEclipseGameStateSubsystem::GetSlotCapacity for the sizes.
//
// There is no "available"/backpack list behind this: if an item isn't
// worn, held, or pocketed, it's on the floor or in a locker.
UENUM(BlueprintType)
enum class EEclipseSlotType : uint8
{
	Head      UMETA(DisplayName = "Head"),
	Eyes      UMETA(DisplayName = "Eyes"),
	Neck      UMETA(DisplayName = "Neck"),
	Top       UMETA(DisplayName = "Top"),
	Bottom    UMETA(DisplayName = "Bottom"),
	Shoes     UMETA(DisplayName = "Shoes"),

	// Carriers.
	Hands     UMETA(DisplayName = "Hands"),
	Pockets   UMETA(DisplayName = "Pockets"),
	// Bumbag  — reserved: a worn item that grants 3 more carry slots.
	//           Add the enumerator + a GetSlotCapacity case when it ships.
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
	// Wearables can also be carried loose — Shades come off and go in a
	// pocket. Same meaning as FEclipseItemRow::Size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EEclipseItemSize Size = EEclipseItemSize::Small;
	// Same as FEclipseItemRow::IconTexture — a baked render shown in the
	// slot instead of a 3-letter abbreviation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> IconTexture;
};
