// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseItemActor.h"
#include "Eclipse.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Data/EclipseItemDefinition.h"
#include "Engine/DataTable.h"
#include "UI/EclipseSwapPromptWidget.h"
#include "GameFramework/PlayerController.h"
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

		// Snap by the RENDERED BOUNDS, not the actor origin. Imported prefabs
		// put their pivot wherever the artist left it — the beer bottle's
		// mesh sits 60 cm BELOW its origin, the lollipop 5 cm above — so
		// moving the origin to the floor leaves the visible mesh buried or
		// floating by that offset. Measuring the gap between the origin and
		// the bottom of the bounds and correcting for it lands every prefab
		// on the floor regardless of where its pivot is.
		FVector BoundsOrigin, BoundsExtent;
		InActor->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
		const float BottomToOrigin = Origin.Z - (BoundsOrigin.Z - BoundsExtent.Z);

		OutNewLocation = Origin;
		// +1 cm so it rests on the floor rather than z-fighting with it.
		OutNewLocation.Z = Hit.ImpactPoint.Z + BottomToOrigin + 1.f;
		return true;
	}
}

void AEclipseItemActor::ApplyMeshFromRow()
{
	if (!bUseRowMesh || !Mesh || ItemId.IsNone()) return;

	UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr;
	UEclipseGameStateSubsystem* State = GI ? GI->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;

	FEclipseItemRow Row;
	bool bGotRow = false;
	if (State)
	{
		bGotRow = State->GetItemRow(ItemId, Row);
	}
#if WITH_EDITOR
	else
	{
		// Editor placement: there's no GameInstance in the editor world, so
		// the subsystem lookup above is unavailable. Read DT_Items directly
		// so a designer dragging an item in sees the right model at once.
		if (UDataTable* DT = LoadObject<UDataTable>(nullptr,
				TEXT("/Game/Justin/Data/DT_Items.DT_Items")))
		{
			const FName Base = UEclipseGameStateSubsystem::GetBaseItemId(ItemId);
			if (const FEclipseItemRow* Found =
					DT->FindRow<FEclipseItemRow>(Base, TEXT("ApplyMeshFromRow"), /*bWarnIfMissing=*/false))
			{
				Row = *Found;
				bGotRow = true;
			}
		}
	}
#endif
	if (!bGotRow || Row.Mesh.IsNull()) return;

	// Synchronous load: this runs on placement / BeginPlay, not per frame,
	// and the actor has nothing to show until the mesh is resident.
	if (UStaticMesh* SM = Row.Mesh.LoadSynchronous())
	{
		if (Mesh->GetStaticMesh() != SM)
		{
			Mesh->SetStaticMesh(SM);
			// Component material OVERRIDES survive a SetStaticMesh, so the
			// placeholder cylinder's MI_ItemDarkBlue would keep painting the
			// real prefab solid blue. Clear them and let the mesh use the
			// materials it shipped with.
			Mesh->EmptyOverrideMaterials();
		}
		const float S = FMath::Max(0.0001f, Row.MeshScale);
		SetActorScale3D(FVector(S));

		// How the thing rests. Authored per row because it's a property of
		// the model's own axes, not of where it was dropped — a baggie that
		// imports standing on its edge should lie flat everywhere, including
		// when it's dropped from a swap.
		Mesh->SetRelativeRotation(Row.MeshRotation);

		if (UMaterialInterface* Mat = Row.MeshMaterial.IsNull() ? nullptr : Row.MeshMaterial.LoadSynchronous())
		{
			for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
			{
				Mesh->SetMaterial(i, Mat);
			}
		}
	}
}

void AEclipseItemActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Row-driven visuals first: the floor snap below traces from the actor's
	// origin, and swapping the mesh can change where that sits.
	ApplyMeshFromRow();

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

	ApplyMeshFromRow();

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
			// Currency special-case: "coins" / "notes" / "cigarettes" don't
			// take inventory slots, they bump a counter. Designer-set
			// CurrencyAmount lets a single actor represent a stack
			// (default 1) — a dropped pack is `cigarettes` with Amount 20.
			// Note this is the loose-count currency, NOT the carryable
			// `pack_cigarettes` / `slim_cigarette` items.
			const bool bIsCoins = (ItemId == TEXT("coins"));
			const bool bIsNotes = (ItemId == TEXT("notes"));
			const bool bIsCigs  = (ItemId == TEXT("cigarettes"));
			if (bIsCoins || bIsNotes || bIsCigs)
			{
				const int32 Amt = FMath::Max(1, CurrencyAmount);
				if      (bIsCoins) State->AddCoins(Amt);
				else if (bIsNotes) State->AddNotes(Amt);
				else               State->AddCigarettes(Amt);
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
			// AddItem fails when you're full. The actor is left alone in that
			// case — OfferSwap takes over below.
			else if (ItemId != NAME_None)
			{
				if (!State->AddItem(MakeRuntimeId()))
				{
					// Full. Rather than refusing in silence, offer the trade:
					// this item beside what you're already carrying, pick one
					// to leave behind. The actor stays in the world and
					// untouched until the prompt calls TakeAfterSwap.
					OfferSwap();
					return;
				}
			}

			ConsumePickup();
		}
	}
}

FName AEclipseItemActor::MakeRuntimeId() const
{
	// Per-instance id so two duplicated actors (Alt-drag in the editor) read
	// as distinct chips. The lookup helpers (GetItemRow / GetClothingRow)
	// strip the "__<actor>" suffix, so row data stays template-keyed.
	return FName(*FString::Printf(TEXT("%s__%s"), *ItemId.ToString(), *GetName()));
}

void AEclipseItemActor::OfferSwap()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	if (!UEclipseSwapPromptWidget::OpenForPickup(PC, this))
	{
		// Nothing tradeable — the pickup just doesn't happen. Logged rather
		// than silent so a genuinely stuck player shows up in the log.
		UE_LOG(LogEclipse, Log, TEXT("ItemActor '%s': full, and nothing to swap for it"),
			*ItemId.ToString());
	}
}

bool AEclipseItemActor::TakeAfterSwap(FName OutgoingId)
{
	if (bPickedUp) return false;

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UEclipseGameStateSubsystem* State = GI ? GI->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!State) return false;

	if (!State->SwapCarriedItem(OutgoingId, MakeRuntimeId())) return false;

	ConsumePickup();
	return true;
}

void AEclipseItemActor::ConsumePickup()
{
	UWorld* World = GetWorld();
	if (!World) return;

	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* State = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
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
