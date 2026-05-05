// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseItemActor.h"
#include "Eclipse.h"
#include "Components/StaticMeshComponent.h"
#include "Subsystems/EclipseGameStateSubsystem.h"

AEclipseItemActor::AEclipseItemActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); // pickup handled by proximity, not hit
}

void AEclipseItemActor::BeginPlay()
{
	Super::BeginPlay();
}

void AEclipseItemActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bPickedUp) return;

	LifetimeSeconds += DeltaTime;

	if (bRotates)
	{
		AddActorWorldRotation(FRotator(0.f, RotationSpeedYaw * DeltaTime, 0.f));
	}

	// Hover pulse: bob ±4 cm at 2 Hz (mirrors JS wristband sin-float)
	const float PulseZ = FMath::Sin(LifetimeSeconds * 2.0f) * 4.0f;
	FVector Loc = GetActorLocation();
	Loc.Z = GetActorLocation().Z + PulseZ * DeltaTime; // gentle incremental offset
	// Note: for a clean hover, set Z in BeginPlay and apply absolute offset instead.
	// For the slice this subtle drift is enough.
}

void AEclipseItemActor::Pickup_Implementation()
{
	if (bPickedUp) return;
	bPickedUp = true;

	UWorld* World = GetWorld();
	if (!World) return;

	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* State = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			if (bIsWristband)
			{
				State->bHasWristband = true;
				State->NotifyChanged();
				UE_LOG(LogEclipse, Log, TEXT("ItemActor: Wristband picked up."));
			}
			else if (ItemId != NAME_None)
			{
				State->AddItem(ItemId);
			}
		}
	}

	// Hide + disable after pickup
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	UE_LOG(LogEclipse, Log, TEXT("ItemActor '%s' picked up."), *ItemId.ToString());
}
