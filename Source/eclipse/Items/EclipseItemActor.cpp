// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseItemActor.h"
#include "Eclipse.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"
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

	// ── Floor snap ──
	// The level's floor isn't at z=0 (each room sits at its own elevation, e.g.
	// bathroom floor ≈ z=178). Items dropped into the level via SpawnActor or
	// placed via the editor would otherwise fall to world origin or float at
	// whatever z the spawn defaults to. Trace down from a bit above the
	// current location to find the actual floor and rest on it.
	{
		const FVector Origin = GetActorLocation();
		const FVector Start  = Origin + FVector(0.f, 0.f, 500.f);
		const FVector End    = Origin - FVector(0.f, 0.f, 5000.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EclipseItemFloorSnap), false, this);
		Params.bTraceComplex = true;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			FVector Snapped = Origin;
			// +5 cm so the mesh visually rests on the floor, not buried in it.
			Snapped.Z = Hit.ImpactPoint.Z + 5.f;
			SetActorLocation(Snapped, /*bSweep=*/false);
			UE_LOG(LogEclipse, Log, TEXT("Item '%s' floor-snapped: z %.1f → %.1f"),
				*ItemId.ToString(), Origin.Z, Snapped.Z);
		}
	}

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

	// Audio cue at the pickup location.
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UEclipseAudioSubsystem* Audio = GI->GetSubsystem<UEclipseAudioSubsystem>())
		{
			Audio->PlaySFXAt(PickupSound, GetActorLocation());
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
