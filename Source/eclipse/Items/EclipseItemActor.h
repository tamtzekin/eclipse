// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EclipseItemActor.generated.h"

/**
 * Placeable item actor — spawned in the level, picked up via EclipseInteractSubsystem.
 * Mirrors the JS `roomItems` array + the wristband torus at index.html ~line 5120+.
 *
 * Every pickup is an instance of this class (or a Blueprint child). The ItemId
 * is a row key into DT_Items (or DT_Tickets for wristband).
 *
 * Pickup prompt: "[E] PICK UP <DisplayName>" shown by WBP_InteractPrompt.
 */
UCLASS()
class ECLIPSE_API AEclipseItemActor : public AActor
{
	GENERATED_BODY()

public:
	AEclipseItemActor();

	// ── Item identity ──
	/** Row name in DT_Items / DT_Tickets. Also used as the pickup-added key in inventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	FName ItemId = NAME_None;

	/** Shown in the interact prompt, e.g. "RED WRISTBAND". Pulled from DT_Items if empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	FText DisplayName;

	/** Ink knot name (see Content/Justin/Dialogue/InkSource/Items/Items.ink).
	 *  When set, interacting with this item opens a dialogue panel instead
	 *  of picking it up immediately — see
	 *  UEclipseDialogueSubsystem::OpenItemDialogue and
	 *  EclipseInteractSubsystem::TryInteract. Leave NAME_None for items that
	 *  should keep the old instant-pickup behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	FName DialogueId = NAME_None;

	/** Optional quest flag set on pickup (e.g. "hasHair", "hasEye"). The
	 *  Pickup_Implementation translates known values into Quest.* booleans
	 *  on the game state. Leave NAME_None for items that don't progress a
	 *  quest beat directly (e.g. the wristband: AddItem itself flips
	 *  GameStateSubsystem::bHasWristband when the id is "wristband"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	FName QuestFlag = NAME_None;

	// ── Interact radius (used by InteractSubsystem) ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	float PickupRadius = 150.f;  // 1.5 m

	// Only meaningful when ItemId is "coins" or "notes" — how much currency
	// this single actor adds to the player's counter on pickup. Default 1
	// so a freshly placed coin = 1 coin; bump higher for a "pile of coins"
	// pickup that gives 10 / 50 / etc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item",
		meta = (ClampMin = "1"))
	int32 CurrencyAmount = 1;

	// ── Visual ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eclipse|Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// ── State ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Item")
	bool bPickedUp = false;

	// ── Actions ──
	/** Called by InteractSubsystem when player presses E near this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Eclipse|Item")
	void Pickup();
	virtual void Pickup_Implementation();

	/** Slow rotation tick flag (mirrors wristband pulse in JS). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	bool bRotates = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	float RotationSpeedYaw = 60.f; // degrees/sec

	// Sound played at the item's location on Pickup. Null-safe — no-op if unset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> PickupSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// Editor-time auto-snap to whatever surface is below — fires on Construction
	// (placement, move, paste, OnLoad). Keeps items locked to the floor / shelf
	// they're meant to sit on so designers can drag them in roughly and have
	// them rest cleanly on collision. Runtime safety net stays in BeginPlay.
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UFUNCTION()
	void HandleHighlightToggled(bool bActive);

	float LifetimeSeconds = 0.f;
};
