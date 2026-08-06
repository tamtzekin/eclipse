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

// Shared floor-snap helper — line trace down from a point above the actor and
// drop the actor onto the first blocking hit. Returns true when something
// caught us; on false the caller may decide to leave the actor where it is or
// log a warning. Used by both OnConstruction (editor placement) and BeginPlay
// (runtime safety net).
namespace
{
	bool TraceDownToSurface(AActor* InActor, FVector& OutNewLocation)
	{
		if (!InActor || !InActor->GetWorld()) return false;

		const FVector Origin = InActor->GetActorLocation();
		const FVector Start  = Origin + FVector(0.f, 0.f, 500.f);
		const FVector End    = Origin - FVector(0.f, 0.f, 5000.f);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(EclipseItemFloorSnap), false, InActor);
		Params.bTraceComplex = true;

		FHitResult Hit;
		if (!InActor->GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return false;
		}

		OutNewLocation = Origin;
		// +5 cm so the mesh visually rests on the floor, not buried in it.
		OutNewLocation.Z = Hit.ImpactPoint.Z + 5.f;
		return true;
	}
}

void AEclipseItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Only auto-snap in the editor world. PIE / packaged builds rely on
	// BeginPlay's snap so items can't be missed if a designer placed them
	// floating without re-saving the level.
#if WITH_EDITOR
	if (UWorld* World = GetWorld())
	{
		if (!World->IsGameWorld())
		{
			FVector Snapped;
			if (TraceDownToSurface(this, Snapped))
			{
				const FVector Before = GetActorLocation();
				if (!Snapped.Equals(Before, 0.5f))
				{
					SetActorLocation(Snapped, /*bSweep=*/false);
					UE_LOG(LogEclipse, Log, TEXT("Item '%s' editor-snapped: z %.1f → %.1f"),
						*ItemId.ToString(), Before.Z, Snapped.Z);
				}
			}
			else
			{
				UE_LOG(LogEclipse, Warning, TEXT("Item '%s' editor-snap: nothing under (%0.0f,%0.0f) — left floating at z=%.1f"),
					*ItemId.ToString(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
			}
		}
	}
#endif
}

void AEclipseItemActor::BeginPlay()
{
	Super::BeginPlay();

	// ── Floor snap ──
	// Runtime safety net: if a designer placed an item floating, or it was
	// spawned at runtime via Spawn-from-class, snap it onto the surface below
	// so it doesn't bob in mid-air. Editor placements get the same treatment
	// in OnConstruction; this catches anything OnConstruction missed.
	{
		FVector Snapped;
		if (TraceDownToSurface(this, Snapped))
		{
			const FVector Origin = GetActorLocation();
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

	UWorld* World = GetWorld();
	if (!World) return;

	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* State = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			// Currency special-case: "coins" / "notes" don't take inventory
			// slots, they bump a counter. Designer-set CurrencyAmount lets
			// a single coin actor represent a stack (default 1).
			const bool bIsCoins = (ItemId == TEXT("coins"));
			const bool bIsNotes = (ItemId == TEXT("notes"));
			if (bIsCoins || bIsNotes)
			{
				const int32 Amt = FMath::Max(1, CurrencyAmount);
				if (bIsCoins) State->AddCoins(Amt);
				else          State->AddNotes(Amt);
			}
			// All other items go through AddItem so they appear in the
			// inventory. To keep duplicated actors (Alt-drag in the editor)
			// as distinct chips, we pass a per-instance runtime id formed as
			// "<base>__<actor-name>". The lookup helpers
			// (UEclipseGameStateSubsystem::GetItemRow / GetClothingRow) strip
			// the suffix when querying DT_Items, so row data stays
			// template-keyed.
			//
			// AddItem itself special-cases the BASE id (e.g. "wristband") for
			// quest-flag side effects (bHasWristband).
			//
			// AddItem fails when the inventory is full — bail out here without
			// hiding/consuming the actor, so a full inventory just blocks the
			// pickup instead of silently destroying the item.
			else if (ItemId != NAME_None)
			{
				const FName RuntimeId = FName(*FString::Printf(
					TEXT("%s__%s"), *ItemId.ToString(), *GetName()));
				if (!State->AddItem(RuntimeId))
				{
					return;
				}
			}

			bPickedUp = true;

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
