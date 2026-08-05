// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePlayerCharacter.h"
#include "Eclipse.h"
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

	// ── Character rotation: faces movement direction, ignores controller ──
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	GetCharacterMovement()->JumpZVelocity = 0.f;   // JS prototype has no jump
	GetCharacterMovement()->AirControl = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = 360.f;
}

void AEclipsePlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

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
	const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
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

void AEclipsePlayerCharacter::Tick(float DeltaTime)
{
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
	// (approaching should read as gradual noticing), fast while DECREASING
	// (walking away should let go promptly, not linger mid-swing).
	const float TargetAlpha    = bFacingTarget ? 1.f : ApproachAlpha;
	const FVector TargetLoc    = bFacingTarget ? FaceTargetLocation : ApproachLocation;
	const float RampSpeed      = (TargetAlpha > DialogueCameraAlpha) ? 1.6f : 12.f;
	DialogueCameraAlpha = FMath::FInterpTo(DialogueCameraAlpha, TargetAlpha, DeltaTime, RampSpeed);

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

		// On the way out (fast/decreasing ramp), stop chasing the NPC's
		// bearing and ease back toward wherever the player is actually
		// facing instead — otherwise the camera stays pinned on the NPC's
		// angle right up until alpha hits zero, then freezes there instead
		// of resuming the normal behind-the-character follow.
		const bool bRetreating = RampSpeed > 1.6f;
		FRotator CamTargetRot = bRetreating ? GetActorRotation() : TargetRot;
		if (!bRetreating)
		{
			CamTargetRot.Yaw -= DialogueCameraAngleOffsetDeg * DialogueCameraAlpha;
		}

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
		// Only actually turn the player's own body once dialogue is open —
		// during the pre-dialogue approach ramp the player is still walking
		// under their own control, so only the camera should swing.
		if (bFacingTarget)
		{
			const FRotator NewActorRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, /*Speed=*/3.f);
			SetActorRotation(NewActorRot);
		}
	}

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
