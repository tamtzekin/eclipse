// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EclipseEntranceTrigger.generated.h"

class UBoxComponent;
class AEclipseNpcCharacter;
class UEclipseDialogueSubsystem;

/**
 * Invisible, solid wall at a room entrance (e.g. the club door) that
 * auto-opens dialogue with a gating NPC the instant the player physically
 * bumps into it, and gently pushes the player back once that conversation
 * ends — so being turned away reads as an actual doorway confrontation, not
 * just bumping an invisible wall. Whether the player is actually LET
 * THROUGH is entirely up to the .ink script (divert the player past this
 * knot / whatever variable it's gating passage on) — this actor only
 * handles the auto-trigger + knockback ceremony around it, it never grants
 * or blocks passage itself.
 *
 * Single box component: solid ("BlockAll"), physically stops the player.
 * UE fires OnComponentHit automatically for a blocking sweep — no separate
 * overlap trigger needed; hitting the wall IS the trigger.
 *
 * Orient the actor so +X (its forward vector) points back the way the
 * player approaches from — KnockbackDistance pushes along -forward.
 */
UCLASS()
class ECLIPSE_API AEclipseEntranceTrigger : public AActor
{
	GENERATED_BODY()

public:
	AEclipseEntranceTrigger();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Eclipse|Entrance")
	TObjectPtr<UBoxComponent> BlockingWall;

	// The NPC to auto-engage the instant the player hits BlockingWall.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Entrance")
	TObjectPtr<AEclipseNpcCharacter> GateNpc;

	// Distance to push the player back (along -GetActorForwardVector) once
	// the dialogue this trigger opened closes. 0 = no knockback.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Entrance")
	float KnockbackDistance = 150.f;

	UFUNCTION()
	void OnClubEntryInvisibleWall(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UFUNCTION()
	void HandleDialogueClosed();

private:
	// True only while THIS trigger's own dialogue is the one open —
	// OnDialogueClosed carries no params, so nothing else identifies which
	// conversation just ended; without this guard, some unrelated NPC's
	// conversation closing elsewhere would also knock the player back here.
	bool bAwaitingOwnDialogueClose = false;

	UEclipseDialogueSubsystem* GetDialogueSubsystem() const;
};
