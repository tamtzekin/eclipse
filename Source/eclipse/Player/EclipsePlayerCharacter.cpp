// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePlayerCharacter.h"
#include "Eclipse.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"

AEclipsePlayerCharacter::AEclipsePlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule + collision defaults (CharacterMovement handles the rest)
	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);

	// ── Spring Arm — Natalia/Blockout (UE 3rd-person template) config ──
	// Mouse drives the controller, controller drives the spring arm. Character
	// rotates only to face movement (via bOrientRotationToMovement). Camera
	// stays put when the character turns — no whip.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 400.0f;
	SpringArm->bUsePawnControlRotation = true;  // mouse drives the camera

	// Camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// Motion blur down from UE's 0.5 default. At 0.5 ordinary walking smears,
	// and the post-dialogue camera swing — which moves fast by design — turns
	// into a streak that reads as a rendering fault rather than a movement.
	// Trimming the max distortion too so a fast swing can't smear across a
	// big slice of the frame. Overridden on the camera itself so it applies
	// wherever the player goes, no per-level PP volume needed.
	Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
	Camera->PostProcessSettings.MotionBlurAmount           = 0.15f;
	Camera->PostProcessSettings.bOverride_MotionBlurMax    = true;
	Camera->PostProcessSettings.MotionBlurMax              = 1.5f;

	// ── Character rotation: faces movement direction, ignores controller ──
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity = 0.f;   // JS prototype has no jump
	GetCharacterMovement()->AirControl = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = BaseWalkSpeed;
}

void AEclipsePlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	LastGroundZ = GetActorLocation().Z;

	// Hand over Globals.ink's already-carried items. Here rather than in a
	// subsystem's Initialize because GameInstance subsystems come up once
	// per process — a NEW GAME from the menu would never re-run it — and
	// because the Ink story has to exist first. SeedStartingKit is a no-op
	// after the first time and on a loaded save.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->SeedStartingKit();
		}
	}

	// ── Floor snap ──
	// Same trick as NPCs / items: line-trace down to the actual floor so the
	// player doesn't spawn floating or buried when the level's floor isn't at
	// z=0. Adds the capsule half-height back so the capsule rests on the floor.
	{
		const FVector Origin = GetActorLocation();
		const FVector Start  = Origin + FVector(0.f, 0.f, 500.f);
		const FVector End    = Origin - FVector(0.f, 0.f, 5000.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EclipsePlayerFloorSnap), false, this);
		Params.bTraceComplex = true;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			const float HalfHeight = GetCapsuleComponent()
				? GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
				: 88.f;
			FVector Snapped = Origin;
			Snapped.Z = Hit.ImpactPoint.Z + HalfHeight;
			SetActorLocation(Snapped, /*bSweep=*/false);
			UE_LOG(LogEclipse, Log, TEXT("Player floor-snapped: z %.1f → %.1f"),
				Origin.Z, Snapped.Z);
		}
	}

	// Wire up the default Enhanced Input mapping context if assigned (BP override).
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, /*Priority=*/0);
			}
		}
	}

	// Cache camera defaults so the TAB-hold zoom can lerp back precisely.
	if (SpringArm) DefaultArmLength = SpringArm->TargetArmLength;
	if (Camera)    DefaultFOV       = Camera->FieldOfView;

	// Subscribe to the InteractSubsystem TAB toggle so we can drive the zoom.
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->OnHighlightToggled.AddDynamic(this, &AEclipsePlayerCharacter::HandleHighlightToggled);
	}

	// ── Save-restore teleport ──
	// If GameStateSubsystem::TryLoadCurrent stashed a pending world transform
	// (because the load happened before this pawn existed — typical of
	// load-on-boot / cross-level loads), apply it now. Runs LAST so it
	// overrides the floor-snap above with the exact saved location.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->ConsumePendingTeleport(this);
		}
	}
}

void AEclipsePlayerCharacter::HandleHighlightToggled(bool bActive)
{
	bHighlightZoomActive = bActive;
}

void AEclipsePlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)        EIC->BindAction(MoveAction,        ETriggerEvent::Triggered, this, &AEclipsePlayerCharacter::OnMove);
		if (LookAction)        EIC->BindAction(LookAction,        ETriggerEvent::Triggered, this, &AEclipsePlayerCharacter::OnLook);
		// Interact is one-shot: Started fires once on press. Triggered would
		// fire every frame the key is held (~30 times for a half-second tap),
		// which used to cascade-pickup every item in range on a single press.
		if (InteractAction)    EIC->BindAction(InteractAction,    ETriggerEvent::Started, this, &AEclipsePlayerCharacter::OnInteract);
		if (HighlightAction)
		{
			EIC->BindAction(HighlightAction, ETriggerEvent::Started,   this, &AEclipsePlayerCharacter::OnHighlightStart);
			EIC->BindAction(HighlightAction, ETriggerEvent::Completed, this, &AEclipsePlayerCharacter::OnHighlightEnd);
		}
	}
}

void AEclipsePlayerCharacter::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	if (Axis.IsNearlyZero() || !Camera) return;

	// Use the camera's facing direction (flattened to the ground plane) so WASD
	// is screen-relative — matches the UE 3rd-person template (Natalia/Blockout):
	// derive a flat-yaw rotator from the controller, build forward+right axes,
	// add input.
	if (!Controller) return;

	// While locked onto an NPC, base movement on the live geometric bearing to
	// them instead of the camera's own rotation. The camera's rotation is
	// itself being scripted every tick to chase that same bearing while
	// locked — using it as the movement basis too closes a feedback loop
	// (camera corrects toward the bearing from the player's new position →
	// that's "forward" for movement → that moves the player → the bearing
	// shifts → camera corrects again), which is what read as a shake when
	// backing straight out with S. Computing forward directly from world
	// positions instead guarantees backing away is always perfectly radial,
	// so the bearing the camera is chasing never actually needs to move.
	// Outside a lock-on, the basis is LATCHED at the start of a movement
	// burst rather than read live. TickFollowCamera swings the control
	// rotation round behind the character on its own, and feeding that back
	// in as the movement basis closes a loop: hold D, the camera chases the
	// character's new heading, "right" rotates with it, and the character
	// walks a circle instead of a straight line. Latching keeps the walk
	// straight while the camera catches up. OnLook drops the latch, so
	// steering with the mouse still re-bases movement on the spot.
	if (!bMoveBasisValid)
	{
		MoveBasisYaw = Controller->GetControlRotation().Yaw;
		bMoveBasisValid = true;
	}

	const float BasisYaw = bFacingTarget
		? FMath::RadiansToDegrees(FMath::Atan2(
			FaceTargetLocation.Y - GetActorLocation().Y,
			FaceTargetLocation.X - GetActorLocation().X))
		: MoveBasisYaw;
	const FRotator YawRot(0.f, BasisYaw, 0.f);
	const FVector  Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
	const FVector  Right   = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right,   Axis.X);
}

void AEclipsePlayerCharacter::OnLook(const FInputActionValue& Value)
{
	if (GetController())
	{
		const FVector2D Axis = Value.Get<FVector2D>();
		if (!Axis.IsNearlyZero())
		{
			// Player is steering. Back the follow-camera off, and re-base
			// movement on where they just pointed the camera.
			LookIdleTime    = 0.f;
			bMoveBasisValid = false;
		}
		AddControllerYawInput(Axis.X);
		// Negate Y so mouse-up looks up (non-inverted Y).
		AddControllerPitchInput(-Axis.Y);
	}
}

void AEclipsePlayerCharacter::OnInteract(const FInputActionValue& /*Value*/)
{
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->TryInteract();
	}
}

void AEclipsePlayerCharacter::OnHighlightStart(const FInputActionValue& /*Value*/)
{
	UE_LOG(LogEclipse, Log, TEXT("[TAB] OnHighlightStart fired"));
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->SetHighlightActive(true);
	}
	else
	{
		UE_LOG(LogEclipse, Warning, TEXT("[TAB] InteractSubsystem not found"));
	}
}

void AEclipsePlayerCharacter::OnHighlightEnd(const FInputActionValue& /*Value*/)
{
	UE_LOG(LogEclipse, Log, TEXT("[TAB] OnHighlightEnd fired"));
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->SetHighlightActive(false);
	}
}

// ─── OTS face-target rotation (called by dialogue subsystem) ────────────────

void AEclipsePlayerCharacter::StartFaceTarget(const FVector& WorldTarget)
{
	bFacingTarget = true;
	FaceTargetLocation = WorldTarget;
}

void AEclipsePlayerCharacter::StopFaceTarget()
{
	bFacingTarget = false;
}

void AEclipsePlayerCharacter::UpdateApproachProximity(float Alpha, const FVector& TargetLocation)
{
	ApproachAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	ApproachLocation = TargetLocation;
}

// ─── Follow camera ──────────────────────────────────────────────────────────
//
// Eases the camera round to sit behind the character. Walking forward already
// looks that way (movement is camera-relative), so what this actually fixes is
// the aftermath of strafing or backing up, where bOrientRotationToMovement
// turns the body but leaves the camera pointing the old way.
//
// Deliberately does nothing while the player is steering with the mouse, or
// while the dialogue camera owns the controller — two systems writing
// ControlRotation in the same frame reads as a fight, not a follow.
void AEclipsePlayerCharacter::TickFollowCamera(float DeltaTime)
{
	LookIdleTime += DeltaTime;

	// Standing still: nothing to follow, and the body's yaw is stale anyway
	// (bOrientRotationToMovement leaves it wherever the last step ended).
	// Drop the movement-basis latch here rather than on key-release so we
	// don't need an input-ordering assumption; braking just holds it a few
	// extra frames, which is harmless.
	const FVector Velocity = GetVelocity();
	if (Velocity.SizeSquared2D() < FMath::Square(10.f))
	{
		bMoveBasisValid = false;
		return;
	}

	if (DialogueCameraAlpha > 0.001f || bReleasing) return;   // dialogue owns the camera
	if (LookIdleTime < FollowCamResumeDelay)          return;   // player is steering

	AController* Ctrl = GetController();
	if (!Ctrl) return;

	FRotator Target = Ctrl->GetControlRotation();
	Target.Yaw = FMath::RadiansToDegrees(FMath::Atan2(Velocity.Y, Velocity.X));
	Ctrl->SetControlRotation(
		FMath::RInterpTo(Ctrl->GetControlRotation(), Target, DeltaTime, FollowCamSpeed));
}

// ─── Item inspection cut ────────────────────────────────────────────────────

void AEclipsePlayerCharacter::FocusOnActor(AActor* Target)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !Target) return;

	FVector Origin, Extent;
	Target->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
	const float Radius = FMath::Max(Extent.Size(), 8.f);

	// Stand off far enough that the subject's bounding sphere fills
	// InspectFillFraction of the frame's half-height. Driving it off the
	// item's own bounds is what stops a cigarette pack and a vodka bottle
	// framing wildly differently.
	const float HalfFOV = FMath::DegreesToRadians(FMath::Clamp(DefaultFOV, 10.f, 170.f) * 0.5f);
	const float Dist = Radius / FMath::Max(FMath::Tan(HalfFOV) * InspectFillFraction, KINDA_SMALL_NUMBER);

	// Come in from the player's side. Framing from anywhere else would cut to
	// a view of the object the player was never looking at.
	FVector Dir = GetActorLocation() - Origin;
	Dir.Z = 0.f;
	Dir = Dir.GetSafeNormal();
	if (Dir.IsNearlyZero()) Dir = -GetActorForwardVector();

	const FVector Eye = Origin + Dir * Dist + FVector(0.f, 0.f, Radius * 0.35f);

	if (!InspectCamera)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.ObjectFlags |= RF_Transient;
		InspectCamera = GetWorld()->SpawnActor<ACameraActor>(Eye, FRotator::ZeroRotator, Params);
	}
	if (!InspectCamera) return;

	InspectCamera->SetActorLocationAndRotation(Eye, (Origin - Eye).Rotation());
	PC->SetViewTargetWithBlend(InspectCamera, InspectBlendSeconds, VTBlend_Cubic, 0.f, true);
}

void AEclipsePlayerCharacter::ClearFocus()
{
	// CloseDialogue is shared by NPC and item conversations, so this runs on
	// every close. Only take the view back if we're the one holding it —
	// otherwise an ordinary NPC chat would kick off a pointless blend.
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !InspectCamera || PC->GetViewTarget() != InspectCamera) return;

	PC->SetViewTargetWithBlend(this, InspectBlendSeconds, VTBlend_Cubic, 0.f, true);
}

// Steps land on distance covered rather than on a clock, so the cadence
// automatically stretches when the stair slowdown shortens the stride.
void AEclipsePlayerCharacter::TickFootsteps(float DeltaTime)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || !Move->IsMovingOnGround()) { FootstepDistance = 0.f; return; }

	if (FootstepSounds.Num() == 0)
	{
		for (int32 i = 1; i <= 5; ++i)
		{
			const FString Path = FString::Printf(
				TEXT("/Game/Justin/Audio/Steps/S_Footstep_%d.S_Footstep_%d"), i, i);
			if (USoundBase* S = LoadObject<USoundBase>(nullptr, *Path))
			{
				FootstepSounds.Add(S);
			}
		}
		if (FootstepSounds.Num() == 0) { FootstepSounds.Add(nullptr); return; }  // don't retry every frame
	}

	const float Speed = GetVelocity().Size2D();
	if (Speed < 10.f) return;
	FootstepDistance += Speed * DeltaTime;
	if (FootstepDistance < FootstepStrideCm) return;
	FootstepDistance = 0.f;

	// Never the same sample twice running — a repeat is the thing the ear
	// picks out as artificial.
	if (FootstepSounds.Num() == 0) return;
	int32 Index = FMath::RandRange(0, FootstepSounds.Num() - 1);
	if (FootstepSounds.Num() > 1 && Index == LastFootstepIndex)
	{
		Index = (Index + 1) % FootstepSounds.Num();
	}
	LastFootstepIndex = Index;

	if (USoundBase* S = FootstepSounds[Index])
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEclipseAudioSubsystem* A = GI->GetSubsystem<UEclipseAudioSubsystem>())
			{
				A->PlaySFXAt(S, GetActorLocation(), FootstepVolume);
			}
		}
	}
}

// Walking UP costs effort, so the character should visibly labour rather
// than gliding up a staircase at full pace.
//
// This measures HEIGHT GAINED, not the floor's slope. The first attempt read
// CurrentFloor.ImpactNormal, which works on a ramp and does nothing at all on
// stairs: real steps are flat-topped boxes, so the normal under the capsule
// is straight up (Z = 1) for all but the instant it clips a step edge, and a
// slope test reads that as level ground. Climb rate is true for both.
void AEclipsePlayerCharacter::TickSlopeSpeed(float DeltaTime)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || DeltaTime <= 0.f) return;

	const float Z = GetActorLocation().Z;
	const float DeltaZ = Z - LastGroundZ;
	const float RawClimb = DeltaZ / DeltaTime;
	LastGroundZ = Z;

	// Absorb the step-up hop into the spring arm, then let it decay back to
	// zero. Without this the camera tracks the capsule exactly and the view
	// jolts upward once per stair tread.
	if (SpringArm)
	{
		if (Move->IsMovingOnGround())
		{
			CameraZOffset = FMath::Clamp(CameraZOffset - DeltaZ,
				-CameraStepMaxOffset, CameraStepMaxOffset);
		}
		CameraZOffset = FMath::FInterpTo(CameraZOffset, 0.f, DeltaTime, CameraStepSmoothing);
		SpringArm->SetRelativeLocation(FVector(0.f, 0.f, CameraZOffset));
	}

	// Step-ups land as discrete hops rather than a steady rise, so the raw
	// rate is spiky — one huge frame per step, zero in between. Averaging it
	// turns that into the smooth "how fast am I gaining height" the speed
	// actually wants; without it the walk speed would stutter per step.
	const bool bGrounded = Move->IsMovingOnGround();
	const float Sample   = bGrounded ? FMath::Max(0.f, RawClimb) : 0.f;
	SmoothedClimbRate = FMath::FInterpTo(SmoothedClimbRate, Sample, DeltaTime, ClimbSmoothing);

	float Target = BaseWalkSpeed;
	if (bGrounded && SmoothedClimbRate > ClimbDeadzone)
	{
		const float K = FMath::Clamp(SmoothedClimbRate / ClimbRateReference, 0.f, 1.f);
		Target = FMath::Lerp(BaseWalkSpeed, BaseWalkSpeed * StairSpeedScale, K);
	}

	Move->MaxWalkSpeed = FMath::FInterpTo(Move->MaxWalkSpeed, Target, DeltaTime, 6.f);

	const bool bSlowed = Target < BaseWalkSpeed * 0.98f;
	if (bSlowed != bWasClimbing)
	{
		bWasClimbing = bSlowed;
		UE_LOG(LogEclipse, Log, TEXT("Movement: %s climb (rate %.0f cm/s, target speed %.0f)"),
			bSlowed ? TEXT("entered") : TEXT("left"), SmoothedClimbRate, Target);
	}
}

void AEclipsePlayerCharacter::Tick(float DeltaTime)
{
	TickSlopeSpeed(DeltaTime);
	Super::Tick(DeltaTime);

	// ── Chapter clock tick ──
	// Life meters no longer drain over time (sweet-spot 0..10 model — values
	// only move on explicit events: items, dialogue effects, etc.). The only
	// per-frame work here is advancing the chapter clock, which still pauses
	// while a dialogue is open so reading doesn't burn game-time.
	if (UGameInstance* GI = GetGameInstance())
	{
		UEclipseDialogueSubsystem* Dlg = GI->GetSubsystem<UEclipseDialogueSubsystem>();
		const bool bDialogueOpen = Dlg && Dlg->IsDialogueOpen();
		if (!bDialogueOpen)
		{
			if (UEclipseGameStateSubsystem* State = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				State->TickChapterClock(DeltaTime);
			}
		}
	}

	// DialogueCameraAlpha drives the camera's angle-offset and zoom-in.
	// Target is continuous, not a binary switch: dialogue actually being
	// open (bFacingTarget) always wins at full strength; otherwise it
	// tracks ApproachAlpha, which EclipseInteractSubsystem feeds in every
	// tick based on live distance to the nearest talkable NPC — so the
	// camera starts gradually reacting as you approach, not just the
	// instant you cross into official talk range. Ramp speed is
	// deliberately asymmetric: slow while the target is INCREASING
	// (approaching should read as gradual noticing), a little quicker while
	// DECREASING so walking away doesn't linger mid-swing — but only a
	// little. The release used to run at 12 and snapped the camera back hard
	// enough to read as a glitch rather than a movement.
	const float RawTargetAlpha = bFacingTarget ? 1.f : ApproachAlpha;
	if (bFacingTarget)
	{
		bReleasing = false; // a deliberate re-lock always wins
	}
	else if (RawTargetAlpha <= DialogueCameraAlpha)
	{
		// Not increasing — releasing (or already fully decayed). Latch it:
		// once we start letting go, commit to decaying all the way to 0
		// rather than re-checking every tick, so a second nearby NPC's
		// ApproachAlpha can't spike back up and pull the camera into chasing
		// it mid-retreat.
		bReleasing = true;
	}
	const float TargetAlpha    = bReleasing ? 0.f : RawTargetAlpha;
	const FVector TargetLoc    = bFacingTarget ? FaceTargetLocation : ApproachLocation;
	const float RampSpeed      = (TargetAlpha > DialogueCameraAlpha) ? 1.6f : 2.2f;
	DialogueCameraAlpha = FMath::FInterpTo(DialogueCameraAlpha, TargetAlpha, DeltaTime, RampSpeed);
	if (bReleasing && DialogueCameraAlpha <= 0.001f)
	{
		bReleasing = false; // fully let go — ready for a fresh approach next time
	}

	if (DialogueCameraAlpha > 0.001f)
	{
		// OTS dialogue pivot — rotate the CONTROLLER (not just the actor) so
		// the SpringArm (bUsePawnControlRotation=true) follows and the camera
		// swings to look at the NPC. Mouse-look is already suppressed by the
		// dialogue widget (SetIgnoreLookInput(true)) once dialogue is
		// actually open, so the controller's scripted yaw won't fight player
		// input; during the pre-dialogue approach ramp the player can still
		// look around freely and this just adds the swing on top.
		//
		// Regression: the previous build only called SetActorRotation, which
		// rotated the player mesh but left the camera frozen because
		// SpringArm follows ControlRotation, not ActorRotation.
		const FVector ToTarget = TargetLoc - GetActorLocation();
		const FRotator TargetRot(0.f, FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X)), 0.f);

		// Body faces the NPC directly — camera sits off to one side of that
		// same bearing instead of dead behind the player's back, so this
		// reads as a two-shot (both characters in frame) rather than a
		// face-on stare. Modest offset — a normal chase-cam angle, not an
		// orbiting establishing shot. Ramps in with DialogueCameraAlpha so
		// the swing-to-angle is part of the same ease as the zoom.
		constexpr float DialogueCameraAngleOffsetDeg = 30.f;

		// bReleasing (latched above) always wins over a raw per-tick alpha
		// comparison — once retreating, stop touching ControlRotation
		// entirely and stay hands-off for the whole retreat, no matter what
		// ApproachAlpha does in the meantime. Normal third-person camera
		// behavior here is that the camera never follows the body at all,
		// only the mouse, so backing out should just return the camera to
		// normal — nothing chasing the NPC's bearing, nothing chasing the
		// player's own body facing, and no re-engaging mid-retreat because a
		// second nearby NPC's ApproachAlpha happened to spike.
		if (!bReleasing)
		{
			FRotator CamTargetRot = TargetRot;
			CamTargetRot.Yaw -= DialogueCameraAngleOffsetDeg * DialogueCameraAlpha;
			// Per-NPC lift, for anyone standing somewhere the default eyeline
			// puts scenery between the camera and them (Zbigniewa behind her
			// wall). Positive pitch looks DOWN in UE, so the camera rises.
			CamTargetRot.Pitch += DialogueCameraPitchOffset * DialogueCameraAlpha;

			if (AController* Ctrl = GetController())
			{
				// Pull strength itself ramps with DialogueCameraAlpha — CamTargetRot
				// jumps straight to the NPC's exact bearing the moment alpha ticks
				// past 0, so without this the camera yanked toward it at full speed
				// from the very first frame of contact instead of easing in with
				// proximity. Floor of 0.3 keeps a faint pull alive at low alpha
				// rather than a dead zone.
				const float RotSpeed = FMath::Lerp(0.3f, 3.f, DialogueCameraAlpha);
				const FRotator NewCtrlRot = FMath::RInterpTo(
					Ctrl->GetControlRotation(), CamTargetRot, DeltaTime, RotSpeed);
				Ctrl->SetControlRotation(NewCtrlRot);
			}
		}
		// Only actually turn the player's own body once dialogue is open —
		// during the pre-dialogue approach ramp the player is still walking
		// under their own control, so only the camera should swing.
		if (bFacingTarget)
		{
			const FRotator NewActorRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, /*Speed=*/3.f);
			SetActorRotation(NewActorRot);
		}
	}

	TickFollowCamera(DeltaTime);
	TickFootsteps(DeltaTime);

	// ── TAB-hold zoom-OUT + reverse-fisheye post-process ──
	//
	// Flipped from the previous "zoom in / first-person reticle" pass: TAB
	// now pulls the camera OUT and widens FOV for a god's-eye scan of the
	// scene. Vignette + chromatic-aberration stay as the visual cue —
	// designer can later swap them out for a real fisheye post-process
	// material by calling `Camera->AddOrUpdateBlendable(FishEyeMat, alpha)`.
	{
		const float Target = bHighlightZoomActive ? 1.f : 0.f;
		HighlightZoomAlpha = FMath::FInterpTo(HighlightZoomAlpha, Target, DeltaTime, /*Speed=*/8.f);

		if (SpringArm)
		{
			// Pull out to ~2x default arm length on full hold.
			SpringArm->TargetArmLength = FMath::Lerp(DefaultArmLength, DefaultArmLength * 2.f, HighlightZoomAlpha);

			// Dialogue zoom-in overrides the above — applied after so it
			// isn't immediately stomped back to DefaultArmLength when TAB
			// isn't held (HighlightZoomAlpha == 0).
			if (DialogueCameraAlpha > 0.001f)
			{
				constexpr float DialogueArmLengthMultiplier = 0.5f;
				SpringArm->TargetArmLength = FMath::Lerp(
					SpringArm->TargetArmLength, DefaultArmLength * DialogueArmLengthMultiplier, DialogueCameraAlpha);
			}
		}
		if (Camera)
		{
			// Widen FOV for the "step back and survey" feel (was 70 / pinch-in).
			Camera->SetFieldOfView(FMath::Lerp(DefaultFOV, 110.f, HighlightZoomAlpha));

			// Layer in vignette + chromatic-aberration via the camera's local
			// PostProcessSettings. bOverride_ flags are required for the values
			// to win over the scene's existing PP volume / fallback.
			FPostProcessSettings& PP = Camera->PostProcessSettings;
			PP.bOverride_VignetteIntensity  = true;
			PP.VignetteIntensity            = FMath::Lerp(0.4f, 1.6f, HighlightZoomAlpha);
			PP.bOverride_SceneFringeIntensity = true;
			PP.SceneFringeIntensity         = FMath::Lerp(0.f, 4.f, HighlightZoomAlpha);
			PP.bOverride_ChromaticAberrationStartOffset = true;
			PP.ChromaticAberrationStartOffset = 0.f;
		}

		// Camera pulls AWAY from the body now, so the player mesh stays
		// visible at all times — no need to hide it like the old zoom-IN
		// path did. If we previously hid it (state carry-over after a
		// rebuild), unhide on the way out.
		if (USkeletalMeshComponent* PlayerMesh = GetMesh())
		{
			if (!PlayerMesh->IsVisible())
			{
				PlayerMesh->SetVisibility(true, /*bPropagateToChildren=*/true);
			}
		}
	}
}
