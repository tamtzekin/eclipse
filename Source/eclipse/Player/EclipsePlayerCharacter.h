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
class ACameraActor;

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

	// Extra pitch for the dialogue framing, in degrees. Carried by the NPC so
	// awkwardly-placed characters can be framed individually.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void SetDialogueCameraPitch(float Degrees) { DialogueCameraPitchOffset = Degrees; }

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

	// Cut the view to a framed shot of Target, sized from its own bounds so
	// a phone and a bottle both fill the frame the same amount. Called by
	// EclipseDialogueSubsystem when an item dialogue opens; ClearFocus puts
	// the view back on the pawn.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void FocusOnActor(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Camera")
	void ClearFocus();

	virtual void Tick(float DeltaTime) override;

	// ── Follow camera ──
	// How fast the camera swings back behind the character once it's off-axis.
	// Deliberately slow: this is a drift, not a snap.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float FollowCamSpeed = 1.5f;
	// Seconds of no mouse-look before the swing resumes, so the camera never
	// fights the player for the stick.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float FollowCamResumeDelay = 1.2f;
	// Blend time for the item-inspection cut. Short enough to read as a cut.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float InspectBlendSeconds = 0.15f;
	// Fraction of the frame's half-height the subject's bounding sphere fills.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float InspectFillFraction = 0.55f;

	// Level walking pace, and how much of it survives a climb.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Movement")
	float BaseWalkSpeed = 360.f;
	// Fraction of BaseWalkSpeed when climbing at ClimbRateReference or more.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Movement")
	float StairSpeedScale = 0.40f;
	// Smoothed height gain (cm/s) at which the full slowdown applies.
	// CALIBRATED FROM THE GAME, not derived: climbing the club stairs logs a
	// smoothed rate of 17-41 cm/s. The first value here was 160, guessed off
	// a model of continuous stepping, which made a real climb read as K~0.2
	// and shaved an invisible 5% off the walk speed. Re-measure with the
	// "Movement: entered climb" log line if the stair geometry changes.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Movement")
	float ClimbRateReference = 35.f;

	// Below this the climb is treated as flat ground. Real stairs sit well
	// above it; the floor's own micro-bumps sit below, and without the
	// deadzone they'd shave a few percent off ordinary walking.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Movement")
	float ClimbDeadzone = 8.f;

	// Stairs move the capsule in discrete vertical hops, and the camera
	// rides every one of them — the whole frame bounces once per step.
	// The arm absorbs each pop and lets it decay, so the camera rises
	// smoothly through what the capsule does in jumps. Higher = stiffer
	// (more bounce gets through), lower = floatier.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float CameraStepSmoothing = 7.f;
	// Cap on how far behind the capsule the camera is allowed to trail, so
	// a big drop can't leave it buried in the floor.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Camera")
	float CameraStepMaxOffset = 45.f;

	float CameraZOffset = 0.f;

	// ── Footsteps ──
	// Driven by distance travelled, not a timer: the stair slowdown changes
	// how fast the character walks, and a timer would keep the same cadence
	// while the stride visibly shortened.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Audio")
	float FootstepStrideCm = 95.f;
	UPROPERTY(EditAnywhere, Category = "Eclipse|Audio")
	float FootstepVolume = 0.55f;
	// Filled from /Game/Justin/Audio/Steps on first use. Several samples so
	// the same clip doesn't retrigger every stride and read as a machine gun.
	UPROPERTY(Transient)
	TArray<TObjectPtr<class USoundBase>> FootstepSounds;

	float FootstepDistance = 0.f;
	int32 LastFootstepIndex = -1;
	void TickFootsteps(float DeltaTime);

	// Set from the NPC being talked to (see AEclipseNpcCharacter). Degrees
	// of extra camera pitch while the dialogue framing is engaged.
	float DialogueCameraPitchOffset = 0.f;
	// How fast the averaged climb rate chases the raw one. Low enough to
	// smooth the per-step hop, high enough to react within a step or two.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Movement")
	float ClimbSmoothing = 3.5f;

	float LastGroundZ       = 0.f;
	float SmoothedClimbRate = 0.f;
	bool  bWasClimbing      = false;

	void TickSlopeSpeed(float DeltaTime);

private:
	void TickFollowCamera(float DeltaTime);

	// Seconds since the player last moved the mouse. TickFollowCamera stays
	// hands-off until this passes FollowCamResumeDelay.
	float LookIdleTime = 0.f;

	// Yaw that WASD is resolved against, latched for the duration of one
	// movement burst. See OnMove for why it can't just read the live control
	// rotation any more.
	float MoveBasisYaw    = 0.f;
	bool  bMoveBasisValid = false;

	// Spawned on first use by FocusOnActor, reused for every later inspect.
	UPROPERTY() TObjectPtr<ACameraActor> InspectCamera;

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
