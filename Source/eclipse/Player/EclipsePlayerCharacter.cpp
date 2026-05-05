// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePlayerCharacter.h"
#include "Eclipse.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Subsystems/EclipseInteractSubsystem.h"

AEclipsePlayerCharacter::AEclipsePlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule + collision defaults (CharacterMovement handles the rest)
	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);

	// ── Spring Arm — RE4-style lazy-follow camera ──
	// Mirrors index.html updateThirdPersonCamera (L1531):
	//   targetCamYaw lerps toward player.facing every frame at speed dt*5
	//   camera position = player + sin(camYaw)*CAM_DISTANCE + shoulder_offset
	//
	// In UE we get the same behaviour by:
	//   - bUsePawnControlRotation = false  → arm doesn't track mouse
	//   - bInheritYaw = true               → arm rotates with the character actor
	//   - bEnableCameraRotationLag = true  → smooth lerp instead of snap
	//
	// Combined with bOrientRotationToMovement on the character below, this
	// gives: player turns to face movement → camera lazily swings behind them.
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 350.f;
	SpringArm->SocketOffset = FVector(0.f, 40.f, 80.f);   // small right-shoulder offset (CAM_SHOULDER)
	SpringArm->bUsePawnControlRotation = false;           // ← key change
	SpringArm->bInheritYaw   = true;
	SpringArm->bInheritPitch = false;                      // pitch fixed; not driven by controller
	SpringArm->bInheritRoll  = false;
	SpringArm->SetRelativeRotation(FRotator(-10.f, 0.f, 0.f)); // small downward pitch
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	SpringArm->bEnableCameraRotationLag = true;            // lazy yaw follow
	SpringArm->CameraRotationLagSpeed = 2.5f;              // gentler than the JS dt*5; tweak in BP if you want snappier
	SpringArm->CameraLagMaxTimeStep = 1.f / 30.f;
	SpringArm->bDoCollisionTest = true;

	// Camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// ── Character rotation: faces movement direction (player.facing in JS) ──
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;   // ← player faces movement
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f); // turn speed
	GetCharacterMovement()->JumpZVelocity = 0.f; // JS prototype has no jump
	GetCharacterMovement()->AirControl = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = 360.f;
}

void AEclipsePlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

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
}

void AEclipsePlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)        EIC->BindAction(MoveAction,        ETriggerEvent::Triggered, this, &AEclipsePlayerCharacter::OnMove);
		if (LookAction)        EIC->BindAction(LookAction,        ETriggerEvent::Triggered, this, &AEclipsePlayerCharacter::OnLook);
		if (InteractAction)    EIC->BindAction(InteractAction,    ETriggerEvent::Triggered, this, &AEclipsePlayerCharacter::OnInteract);
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
	// is screen-relative, exactly like the JS prototype. Because the camera
	// lazy-follows the player, this typically matches the player's forward —
	// but during the camera's catch-up, input still feels intuitive (W = "into
	// the screen", D = "screen-right").
	const FRotator CamYaw(0.f, Camera->GetComponentRotation().Yaw, 0.f);
	const FVector  Forward = FRotationMatrix(CamYaw).GetUnitAxis(EAxis::X);
	const FVector  Right   = FRotationMatrix(CamYaw).GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right,   Axis.X);

	// Rotate slower when reversing so the camera doesn't whip behind you when
	// you tap S — matches the JS prototype's softer transition on backward
	// movement. Forward & strafe → snappy; reverse → ~3× slower.
	if (UCharacterMovementComponent* CM = GetCharacterMovement())
	{
		const bool bReversing = Axis.Y < -0.1f && FMath::Abs(Axis.X) < 0.5f;
		const float DesiredRate = bReversing ? 180.f : 540.f;
		if (!FMath::IsNearlyEqual(CM->RotationRate.Yaw, DesiredRate))
		{
			CM->RotationRate = FRotator(0.f, DesiredRate, 0.f);
		}
	}
}

void AEclipsePlayerCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
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
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->SetHighlightActive(true);
	}
}

void AEclipsePlayerCharacter::OnHighlightEnd(const FInputActionValue& /*Value*/)
{
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

void AEclipsePlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bFacingTarget)
	{
		// Smoothly rotate the character (and via spring-arm-inherit, the camera)
		// to face the target's horizontal position. Lerp speed ~5 rad/s matches
		// the JS prototype's camYaw lerp factor.
		const FVector ToTarget = FaceTargetLocation - GetActorLocation();
		const FRotator TargetRot(0.f, FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X)), 0.f);
		const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, /*Speed=*/6.f);
		SetActorRotation(NewRot);
	}
}
