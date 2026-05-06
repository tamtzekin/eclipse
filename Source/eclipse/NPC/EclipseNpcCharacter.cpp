// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseNpcCharacter.h"
#include "Eclipse.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "UI/EclipseSpeechBubbleWidget.h"

namespace
{
	UMaterialInterface* GetHighlightOverlay()
	{
		static TWeakObjectPtr<UMaterialInterface> Cache;
		if (!Cache.IsValid())
		{
			Cache = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Justin/Materials/M_HighlightOverlay.M_HighlightOverlay"));
		}
		return Cache.Get();
	}
}

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
	UMaterialInterface* Overlay = bShow ? GetHighlightOverlay() : nullptr;
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
