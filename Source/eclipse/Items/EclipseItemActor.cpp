// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseItemActor.h"
#include "Eclipse.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "UI/EclipseUiStyle.h"

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

	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->OnHighlightToggled.AddDynamic(this, &AEclipseItemActor::HandleHighlightToggled);
	}
}

void AEclipseItemActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		if (UEclipseInteractSubsystem* IS = W->GetSubsystem<UEclipseInteractSubsystem>())
		{
			IS->OnHighlightToggled.RemoveDynamic(this, &AEclipseItemActor::HandleHighlightToggled);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AEclipseItemActor::HandleHighlightToggled(bool bActive)
{
	UE_LOG(LogEclipse, Log, TEXT("[TAB] Item '%s' HandleHighlightToggled(%s)"),
		*ItemId.ToString(), bActive ? TEXT("on") : TEXT("off"));

	const bool bSuppress = bPickedUp || IsHidden();
	const bool bShow     = bActive && !bSuppress;

	// Pulsing cyan rim-glow overlay on the mesh.
	UMaterialInterface* Overlay = bShow ? EclipseUI::GetHighlightOverlay() : nullptr;
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* M : Meshes)
	{
		if (M) M->SetOverlayMaterial(Overlay);
	}
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
	Loc.Z = GetActorLocation().Z + PulseZ * DeltaTime;
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

			// Apply the configured QuestFlag onto the quest state. Names match
			// the JS prototype's flag keys ("hasHair", "hasEye"); the dialogue
			// subsystem reads State->Quest.bHasHair / bHasEye to pick the right
			// AngelSeeker branch.
			if (QuestFlag == TEXT("hasHair"))
			{
				State->Quest.bHasHair = true;
				State->NotifyChanged();
				UE_LOG(LogEclipse, Log, TEXT("ItemActor '%s': Quest.bHasHair = true"), *ItemId.ToString());
			}
			else if (QuestFlag == TEXT("hasEye"))
			{
				State->Quest.bHasEye = true;
				State->NotifyChanged();
				UE_LOG(LogEclipse, Log, TEXT("ItemActor '%s': Quest.bHasEye = true"), *ItemId.ToString());
			}
		}
	}

	// Hide + clear overlay after pickup
	SetActorHiddenInGame(true);
	SetActorTickEnabled(false);
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* M : Meshes) if (M) M->SetOverlayMaterial(nullptr);
	UE_LOG(LogEclipse, Log, TEXT("ItemActor '%s' picked up."), *ItemId.ToString());
}
