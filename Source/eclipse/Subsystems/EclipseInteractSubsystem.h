// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EclipseInteractSubsystem.generated.h"

class AEclipseNpcCharacter;
class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseNearTalkableChanged, AEclipseNpcCharacter*, Npc);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseNearItemChanged,     AActor*,               Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseHighlightToggled,    bool,                  bOn);

/**
 * World-level subsystem that runs the JS `nearTalkable` / `nearItem` per-frame
 * loops (index.html ~line 4607). Listens for the player's pawn moving and
 * recomputes the nearest interactable, broadcasting changes.
 *
 * The HUD interact-prompt widget binds to OnNearTalkableChanged /
 * OnNearItemChanged. The TAB-held highlight is toggled here too.
 *
 * Honors:
 *   - NPC TalkRadius (200 default, 320 for key NPCs)
 *   - bIsKeyNPC bypasses Thirst=0 block (Bartenders, Cloakroom Lady, VIP Bartender)
 */
UCLASS()
class ECLIPSE_API UEclipseInteractSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// UTickableWorldSubsystem
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEclipseInteractSubsystem, STATGROUP_Tickables); }

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Interact")
	bool TryInteract();

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Interact")
	void SetHighlightActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Eclipse|Interact")
	AEclipseNpcCharacter* GetNearTalkable() const { return NearTalkable; }

	UFUNCTION(BlueprintPure, Category = "Eclipse|Interact")
	AActor* GetNearItem() const { return NearItem; }

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Interact")
	FEclipseNearTalkableChanged OnNearTalkableChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Interact")
	FEclipseNearItemChanged     OnNearItemChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Interact")
	FEclipseHighlightToggled    OnHighlightToggled;

private:
	UPROPERTY() TObjectPtr<AEclipseNpcCharacter> NearTalkable;
	UPROPERTY() TObjectPtr<AActor> NearItem;
	bool bHighlightActive = false;

	// TalkableLockOn — once an NPC has been the nearest talkable for
	// FaceDelaySeconds, it turns to face the player (StartFacePlayer),
	// as if it just noticed them. Reset/released the instant NearTalkable
	// changes to someone else or to nobody.
	float NearTalkableTimer = 0.f;
	TWeakObjectPtr<AEclipseNpcCharacter> FacingNpc;
};
