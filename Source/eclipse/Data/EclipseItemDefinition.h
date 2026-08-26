// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
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

// Whether an item fits in a pocket. Large items have to be carried in the
// hands (or worn, if they're clothing). Drives where AddItem is allowed to
// put a pickup — see UEclipseGameStateSubsystem::CanPlaceInSlot.
UENUM(BlueprintType)
enum class EEclipseItemSize : uint8
{
	Small UMETA(DisplayName = "Small (pocketable)"),
	Large UMETA(DisplayName = "Large (hands only)"),
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
	//   Molly:     HeatDelta = +1                             (sweat)
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 HeatDelta         = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ThirstDelta       = 0;

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
	// Small items can go in a pocket; Large ones occupy the hands. Default
	// Small: most things in this game are bottles, baggies and trinkets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) EEclipseItemSize Size = EEclipseItemSize::Small;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FEclipseItemEffect Effect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Duration = 0.f;    // 0 = permanent / non-timed

	// Permanent stat gain applied once when the item is USED, then the item
	// is consumed. Routed through ApplyStatDelta, so the levels are added to
	// the stat itself and outlive the item — this is not a timed buff and
	// nothing removes it later. StatBoost names the stat; StatBoostLevels is
	// how many levels it moves (negative is allowed and clamps at 0).
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName StatBoost;         // "aesthetics"|"rhythm"|"zen"|"psychedelics"|None
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StatBoostLevels = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName QuestFlag;         // e.g. "hasHair" — for Angel's Hair drink

	// ── Visual ──
	// The mesh every actor with this Id wears. Set it once here and every
	// pickup of that item — hand-placed in a level, dropped from a swap,
	// spawned at runtime — picks it up automatically. Swapping in a
	// designer's custom model later means changing this one row, not
	// re-touching placed actors.
	// Soft pointer: DT_Items is loaded at startup and hard refs here would
	// drag every item mesh into memory with it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> Mesh;

	// Uniform scale applied on top of Mesh. Needed because the source
	// prefabs are authored in wildly different units — as imported, the pig
	// coin is 100 m across and the beer bottle is 2.5 cm tall. Keeping the
	// correction here rather than baking it into the asset means a
	// replacement model just resets this to 1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0001"))
	float MeshScale = 1.f;

	// Optional override material. Left unset the mesh keeps whatever
	// material it imported with.
	// Resting orientation for the mesh, applied on top of the actor's own
	// rotation. Fixes prefabs that import standing on end (the baggie) so
	// they lie the way the object actually would.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator MeshRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UMaterialInterface> MeshMaterial;
};
