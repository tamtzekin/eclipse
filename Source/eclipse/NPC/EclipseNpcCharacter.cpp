// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseNpcCharacter.h"
#include "Eclipse.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/EclipseSpeechBubbleWidget.h"

AEclipseNpcCharacter::AEclipseNpcCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCharacterMovement()->bOrientRotationToMovement = true;

	// Speech bubble — UWidgetComponent renders the UMG widget in 3D world space
	// and billboards toward the camera (Screen draw mode). Positioned ~140 units
	// above the actor pivot to sit above the head.
	BubbleWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("BubbleWidget"));
	BubbleWidget->SetupAttachment(RootComponent);
	BubbleWidget->SetRelativeLocation(FVector(0.f, 0.f, 140.f));
	BubbleWidget->SetWidgetSpace(EWidgetSpace::Screen);   // 2D billboard above head
	BubbleWidget->SetDrawSize(FVector2D(80.f, 60.f));
	BubbleWidget->SetPivot(FVector2D(0.5f, 1.f));         // bottom-center pivot
	BubbleWidget->SetWidgetClass(UEclipseSpeechBubbleWidget::StaticClass());
	// Default class — BP can override via BubbleWidgetClass property + the
	// BeginPlay swap below.

	BubbleWidgetClass = UEclipseSpeechBubbleWidget::StaticClass();
}

void AEclipseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (bStationary)
	{
		// Disable AI tick / movement input — set dressing characters don't wander.
		GetCharacterMovement()->DisableMovement();
	}

	// If a BP designer set a custom bubble widget class, swap it in now.
	if (BubbleWidget && BubbleWidgetClass &&
		BubbleWidget->GetWidgetClass() != BubbleWidgetClass)
	{
		BubbleWidget->SetWidgetClass(BubbleWidgetClass);
	}

	RefreshBubble(/*bMuted=*/false);

	UE_LOG(LogEclipse, Verbose, TEXT("NPC '%s' ready (talkable=%d key=%d radius=%.0f)"),
		*NpcName.ToString(), bTalkable, bIsKeyNPC, TalkRadius);
}

void AEclipseNpcCharacter::RefreshBubble(bool bMuted)
{
	if (!BubbleWidget) return;

	// Hide bubble entirely for non-talkable / hidden NPCs
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
