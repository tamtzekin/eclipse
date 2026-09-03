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
	float PickupRadius = 190.f;  // 1.9 m

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

	// Bare pivot the mesh hangs off — see the constructor for why the mesh
	// is no longer the root.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eclipse|Item")
	TObjectPtr<class USceneComponent> Root;

	// Pull Mesh/MeshScale/MeshMaterial off this actor's DT_Items row and
	// apply them. Runs on editor placement (so a dropped-in actor shows its
	// real model immediately in the viewport), on BeginPlay, and after a
	// runtime spawn. Safe to call repeatedly.
	//
	// Set bUseRowMesh=false on an instance a level artist has dressed by
	// hand, and the row's mesh is left alone.
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Eclipse|Item")
	void ApplyMeshFromRow();

	// When false this actor keeps whatever mesh is set on it in the level
	// and ignores the DataTable. For one-off set dressing.
	//
	// NOT named bApplyMeshFromRow: UE strips the leading 'b' when exposing a
	// bool to Python/Blueprint, so that name resolves to the same symbol as
	// the ApplyMeshFromRow() function above and silently shadows it —
	// `actor.apply_mesh_from_row()` then fails with "'bool' object is not
	// callable".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	bool bUseRowMesh = true;

	// ── State ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Item")
	bool bPickedUp = false;

	// Ambient "you could pick this up" rim — see TickProximityGlow. Which
	// item gets it is the InteractSubsystem's call, not a radius of ours.
	bool bProximityGlowOn  = false;
	bool bTabHighlighted   = false;
	void TickProximityGlow();

	// ── Actions ──
	/** Called by InteractSubsystem when player presses E near this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Eclipse|Item")
	void Pickup();
	virtual void Pickup_Implementation();

	// "<ItemId>__<actor-name>" — the per-instance id this actor occupies in
	// the inventory. Public because the swap prompt needs to name it.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Items")
	FName MakeRuntimeId() const;

	// Give up `OutgoingId` (it lands back in the room) and take this item in
	// its place, then consume the actor exactly as a normal pickup would.
	// Called by UEclipseSwapPromptWidget. False leaves everything untouched.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Items")
	bool TakeAfterSwap(FName OutgoingId);

	// Per-actor override for the pickup sound. Normally left empty — the
	// sound belongs to the KIND of object, so it lives on the DT_Items row
	// (see ResolvePickupSound).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> PickupSound;

	// This actor's override, else the DT_Items row's sound, else null. A
	// coin should clink and a baggie should rustle, and which of those it is
	// is a property of the item, not of the actor someone dragged into the
	// level — so the row is where it's authored.
	class USoundBase* ResolvePickupSound() const;

protected:
	// Opens the swap prompt for this pickup. No-op when there's no player
	// controller or nothing worth trading.
	void OfferSwap();

	// The tail of a successful pickup: quest flags, sound, hide the actor.
	// Shared by the normal path and the post-swap path so a swapped-in item
	// behaves identically to one picked up with room to spare.
	void ConsumePickup();

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
