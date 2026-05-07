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
	// Verbatim from the UE 3rd-person template (eclipseCharacter::DoLook).
	if (GetController())
	{
		const FVector2D Axis = Value.Get<FVector2D>();
		AddControllerYawInput(Axis.X);
		AddControllerPitchInput(Axis.Y);
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

void AEclipsePlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bFacingTarget)
	{
		// Smoothly rotate the character to face the target's horizontal position.
		const FVector ToTarget = FaceTargetLocation - GetActorLocation();
		const FRotator TargetRot(0.f, FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Y, ToTarget.X)), 0.f);
		const FRotator NewRot = FMath::RInterpTo(GetActorRotation(), TargetRot, DeltaTime, /*Speed=*/6.f);
		SetActorRotation(NewRot);
	}
}
