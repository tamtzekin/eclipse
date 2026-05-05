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

	/** Optional quest flag set on pickup (e.g. "hasWristband"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	FName QuestFlag = NAME_None;

	/** If true, calling Pickup() sets bHasWristband on GameStateSubsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	bool bIsWristband = false;

	// ── Interact radius (used by InteractSubsystem) ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Item")
	float PickupRadius = 150.f;  // 1.5 m

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

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	float LifetimeSeconds = 0.f;
};
