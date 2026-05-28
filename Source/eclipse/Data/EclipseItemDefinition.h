// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseItemDefinition.generated.h"

UENUM(BlueprintType)
enum class EEclipseItemType : uint8
{
	// Single-use, consumed on USE (drinks, pills, food). Inventory surfaces
	// these as a USE button.
	Usable     UMETA(DisplayName = "Consumable"),

	// Worn until manually unequipped. Effects (heat/cool/drain modifiers)
	// apply continuously while equipped. Inventory surfaces these as EQUIP.
	Equippable UMETA(DisplayName = "Wearable"),

	// Quest items — held but not consumable. Dialogue / quest beats read
	// the item via QuestFlag and decide outcomes. Hair + Eye are Key. The
	// inventory surfaces these read-only (USE / EQUIP both disabled, only
	// DROP works — and only if the designer marks them droppable later).
	Key        UMETA(DisplayName = "Key")
};

USTRUCT(BlueprintType)
struct FEclipseItemEffect
{
	GENERATED_BODY()

	// Equip-time modifiers — Equippable items, applied continuously while worn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float SpeedMult        = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CoolRate         = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float HeatGainMult     = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float ThirstDrainMult  = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bMotionBlur      = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bRevealNPCs      = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bDarken          = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool  bCharisma        = false;

	// Use-time effects — Usable items, applied once when consumed via
	// UseItem. Each delta is a signed integer on the meter's 0..10 scale.
	// Sweet-spot model: + and - both have valid uses depending on where
	// the meter sits and what the item is supposed to do. Examples:
	//   water:     ThirstDelta = +2                            (dry → hydrated)
	//   beer:      ThirstDelta = +2,  HeatDelta = -1            (hydrate + cool)
	//   gum:       ThirstDelta = -1                            (dries the mouth)
	//   lollipop:  ThirstDelta = +1                            (saliva)
	//   Molly:     StimulationDelta = +2, HeatDelta = +1        (tweak + sweat)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HeatDelta         = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ThirstDelta       = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StimulationDelta  = 0;

	// Legacy float field kept for backward compat with DT rows authored
	// against the pre-refactor 0..100 model. UseItem checks the int
	// deltas first; if all three are 0 AND RestoreThirst > 0 it falls
	// back to the legacy field (scaled by /10 into a positive
	// ThirstDelta, since the new orientation is "high = hydrated").
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RestoreThirst    = 0.f;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EEclipseItemType Type = EEclipseItemType::Usable;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FEclipseItemEffect Effect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Duration = 0.f;    // 0 = permanent / non-timed
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName StatBoost;         // "aesthetics"|"stimulation"|"rhythm"|"zen"|"psychedelics"|None
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName QuestFlag;         // e.g. "hasHair" — for Angel's Hair drink
};
