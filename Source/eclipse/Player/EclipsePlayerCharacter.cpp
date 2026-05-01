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

AEclipsePlayerCharacter::AEclipsePlayerCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule + collision defaults (CharacterMovement handles the rest)
	GetCapsuleComponent()->InitCapsuleSize(34.f, 88.f);

	// Spring Arm — third-person, behind + above, matches JS prototype's CAM_DISTANCE/HEIGHT
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 350.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 80.f);
	SpringArm->bUsePawnControlRotation = true;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 12.f;
	SpringArm->bDoCollisionTest = true; // free wall-pull-in (replaces JS raycast)

	// Camera
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// Movement defaults
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	GetCharacterMovement()->JumpZVelocity = 0.f; // JS prototype has no jump
	GetCharacterMovement()->AirControl = 0.f;
	GetCharacterMovement()->MaxWalkSpeed = 360.f; // ~3.6 m/s, tweak vs JS feel
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
	if (Controller && !Axis.IsNearlyZero())
	{
		const FRotator YawRot(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRot).GetUnitAxis(EAxis::X);
		const FVector Right   = FRotationMatrix(YawRot).GetUnitAxis(EAxis::Y);
		AddMovementInput(Forward, Axis.Y);
		AddMovementInput(Right,   Axis.X);
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
	// TODO(slice): forward to UEclipseInteractSubsystem::TryInteract on the world.
	UE_LOG(LogEclipse, Verbose, TEXT("Player::OnInteract"));
}

void AEclipsePlayerCharacter::OnHighlightStart(const FInputActionValue& /*Value*/)
{
	// TODO(slice): broadcast to UEclipseInteractSubsystem to enable cyan emissive
	// on all interact meshes (mirrors JS TAB-held outline behavior).
}

void AEclipsePlayerCharacter::OnHighlightEnd(const FInputActionValue& /*Value*/)
{
	// TODO(slice): turn highlight off.
}
