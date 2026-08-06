// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "EclipsePlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;

/**
 * Player character — third-person, Spring Arm camera (matches the JS prototype's
 * updateThirdPersonCamera). Movement uses default CharacterMovementComponent.
 *
 * Input actions wired via Enhanced Input — see Config/DefaultInput.ini and the
 * IMC_PlayerDefault asset created in editor.
 *
 * Bathroom-slice scope: walk + interact (E) + highlight (TAB hold).
 * Out of slice: dump-clothes (Q), inventory (I), teleport debug (T).
 */
UCLASS()
class ECLIPSE_API AEclipsePlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEclipsePlayerCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// ── Camera ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;

	// ── Input ──
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> HighlightAction;

	// ── Input handlers ──
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnHighlightStart(const FInputActionValue& Value);
	void OnHighlightEnd(const FInputActionValue& Value);

public:
	// Smoothly rotate the character to face the given world position. Used by
	// EclipseDialogueSubsystem on OpenDialogue — matches the JS prototype's
	// over-the-shoulder transition before the dialogue panel slides in.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void StartFaceTarget(const FVector& WorldTarget);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void StopFaceTarget();

	// Continuous (not delayed/binary) proximity toward the nearest talkable
	// NPC — called every tick by EclipseInteractSubsystem so the dialogue
	// camera framing ramps in gradually as the player approaches, instead
	// of snapping in only once officially "locked on". Alpha 0 = too far to
	// react at all, 1 = fully within talk range. Has no effect while
	// bFacingTarget is true (dialogue open / already locked on wins).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void UpdateApproachProximity(float Alpha, const FVector& TargetLocation);

	virtual void Tick(float DeltaTime) override;

private:
	bool    bFacingTarget = false;
	FVector FaceTargetLocation = FVector::ZeroVector;

	// See UpdateApproachProximity.
	float   ApproachAlpha = 0.f;
	FVector ApproachLocation = FVector::ZeroVector;

	// Dialogue camera framing — 0 = normal follow-cam, 1 = fully angled/zoomed
	// two-shot. Blends toward 1 while bFacingTarget is true and back toward 0
	// once it's false, so closing a conversation eases the camera back out
	// instead of just releasing control mid-angle.
	float DialogueCameraAlpha = 0.f;

	// Latches true the instant we stop being locked/approaching (bFacingTarget
	// goes false while DialogueCameraAlpha isn't increasing) and stays true
	// until DialogueCameraAlpha fully decays to 0. While latched, TargetAlpha
	// is forced to 0 regardless of ApproachAlpha — so passing near a SECOND
	// nearby NPC while backing away from the first can't spike the target back
	// up and yank the camera into chasing it mid-retreat. Cleared immediately
	// on a deliberate re-lock (bFacingTarget true again).
	bool bReleasing = false;

	// ── TAB-hold focus zoom ──
	// Subscribed to UEclipseInteractSubsystem::OnHighlightToggled. While true,
	// Tick lerps SpringArm length, camera FOV, and a few PostProcess settings
	// (vignette + chromatic aberration) to a "scope-in / fisheye focus" look.
	UFUNCTION()
	void HandleHighlightToggled(bool bActive);

	bool  bHighlightZoomActive = false;
	float HighlightZoomAlpha   = 0.f;   // 0..1 lerped per Tick

	// Cached defaults captured at BeginPlay so we can lerp back precisely.
	float DefaultArmLength = 400.f;
	float DefaultFOV       = 90.f;
};
