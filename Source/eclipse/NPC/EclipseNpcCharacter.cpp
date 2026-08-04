// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseNpcCharacter.h"
#include "Eclipse.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "UI/EclipseSpeechBubbleWidget.h"
#include "UI/EclipseUiStyle.h"

AEclipseNpcCharacter::AEclipseNpcCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// Speech bubble — UWidgetComponent renders a UMG widget in 3D world space and
	// billboards toward the camera. ~140cm above the actor pivot, above the head.
	BubbleWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("BubbleWidget"));
	BubbleWidget->SetupAttachment(RootComponent);
	BubbleWidget->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	BubbleWidget->SetWidgetSpace(EWidgetSpace::Screen);
	BubbleWidget->SetDrawSize(FVector2D(80.f, 60.f));
	BubbleWidget->SetPivot(FVector2D(0.5f, 1.f));
	BubbleWidget->SetWidgetClass(UEclipseSpeechBubbleWidget::StaticClass());

	BubbleWidgetClass = UEclipseSpeechBubbleWidget::StaticClass();
}

void AEclipseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();

	// ── Floor snap ──
	// Line-trace down so the NPC rests on the actual floor (which isn't at z=0
	// in this project — each room sits at its own elevation). Capsule
	// half-height puts the actor's pivot at the capsule centre, so the floor-z
	// offset has to add it back.
	if (!bSkipFloorSnap)
	{
		const FVector Origin = GetActorLocation();
		const FVector Start  = Origin + FVector(0.f, 0.f, 500.f);
		const FVector End    = Origin - FVector(0.f, 0.f, 5000.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EclipseNpcFloorSnap), false, this);
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
			UE_LOG(LogEclipse, Log, TEXT("NPC '%s' floor-snapped: z %.1f → %.1f"),
				*NpcName.ToString(), Origin.Z, Snapped.Z);
		}
	}

	// Cache the spawn pose so StepAside() lerps from the right (post-snap) start.
	OriginalLocation = GetActorLocation();

	if (bStationary)
	{
		GetCharacterMovement()->DisableMovement();
	}

	if (BubbleWidget && BubbleWidgetClass &&
		BubbleWidget->GetWidgetClass() != BubbleWidgetClass)
	{
		BubbleWidget->SetWidgetClass(BubbleWidgetClass);
	}

	RefreshBubble(/*bMuted=*/false);

	// Subscribe to the world's InteractSubsystem TAB-toggle delegate so we can
	// pop the rim glow in/out without per-actor ticking.
	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->OnHighlightToggled.AddDynamic(this, &AEclipseNpcCharacter::HandleHighlightToggled);
	}

	UE_LOG(LogEclipse, Verbose, TEXT("NPC '%s' ready (talkable=%d key=%d radius=%.0f)"),
		*NpcName.ToString(), bTalkable, bIsKeyNPC, TalkRadius);
}

void AEclipseNpcCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* W = GetWorld())
	{
		if (UEclipseInteractSubsystem* IS = W->GetSubsystem<UEclipseInteractSubsystem>())
		{
			IS->OnHighlightToggled.RemoveDynamic(this, &AEclipseNpcCharacter::HandleHighlightToggled);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AEclipseNpcCharacter::HandleHighlightToggled(bool bActive)
{
	UE_LOG(LogEclipse, Log, TEXT("[TAB] NPC '%s' HandleHighlightToggled(%s)"),
		*NpcName.ToString(), bActive ? TEXT("on") : TEXT("off"));

	// Don't surface highlights for hidden / non-talkable NPCs (no point hinting
	// "you can talk to them" for set-dressing characters).
	const bool bSuppress = bIsHidden || !bTalkable;
	const bool bShow     = bActive && !bSuppress;

	// Pulsing cyan rim-glow overlay on every mesh component. The overlay slot
	// is rendered as a separate translucent pass over the mesh, so the user's
	// existing materials are not mutated.
	UMaterialInterface* Overlay = bShow ? EclipseUI::GetHighlightOverlay() : nullptr;
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* M : Meshes)
	{
		if (M)
		{
			M->SetOverlayMaterial(Overlay);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  StepAside — quest-triggered slide-out of the way (e.g. AngelSeeker after
//  the player picks "Open the stall and enter"). Lerps from OriginalLocation
//  to OriginalLocation + StepAsideOffset over ~1/StepAsideSpeed seconds.
// ─────────────────────────────────────────────────────────────────────────────

void AEclipseNpcCharacter::StepAside()
{
	if (bSteppingAside) return;
	bSteppingAside = true;
	StepAsideAlpha = 0.f;
	UE_LOG(LogEclipse, Log, TEXT("NPC '%s' StepAside (offset %s)"),
		*NpcName.ToString(), *StepAsideOffset.ToString());
}

void AEclipseNpcCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (bSteppingAside && StepAsideAlpha < 1.f)
	{
		StepAsideAlpha = FMath::Min(1.f, StepAsideAlpha + DeltaTime * StepAsideSpeed);
		const float T = FMath::SmoothStep(0.f, 1.f, StepAsideAlpha);
		const FVector Target = OriginalLocation + StepAsideOffset;
		SetActorLocation(FMath::Lerp(OriginalLocation, Target, T));
	}
}

void AEclipseNpcCharacter::RefreshBubble(bool bMuted)
{
	if (!BubbleWidget) return;

	if (!bTalkable || bIsHidden || BubbleType == EEclipseBubbleType::None)
	{
		BubbleWidget->SetVisibility(false);
		return;
	}

	BubbleWidget->SetVisibility(true);
	if (UEclipseSpeechBubbleWidget* BW = Cast<UEclipseSpeechBubbleWidget>(BubbleWidget->GetUserWidgetObject()))
	{
		BW->SetBubble(BubbleType, bMuted);
	}
}
