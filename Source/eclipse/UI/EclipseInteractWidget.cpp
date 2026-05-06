// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseInteractWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Items/EclipseItemActor.h"
#include "Subsystems/EclipseInteractSubsystem.h"

// ─────────────────────────────────────────────────────────────
//  Initialize — build tree before BindWidget pass
//
//  CSS source (#interact-3d):
//      color: #51eefc; font-size: 13px; font-family: BMSPA;
//      letter-spacing: 2px; text-shadow: 0 0 8px rgba(81,238,252,0.5);
//
//  We render at ~28pt because UE viewport screen-pixels are bigger than
//  HTML's 13px reads in practice, and the user requested it bigger.
// ─────────────────────────────────────────────────────────────

bool UEclipseInteractWidget::Initialize()
{
	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("PromptText"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		PromptText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("PromptText"));

		// Bottom-center, ~75% down — matches HTML #interact-3d which is positioned
		// at the screen-projected NPC head, but as a debug fallback we anchor at
		// 50%/85% so it's always visible while we develop.
		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(PromptText))
		{
			CSlot->SetAnchors(FAnchors(0.5f, 0.85f, 0.5f, 0.85f));
			CSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CSlot->SetAutoSize(true);
		}

		// Font: BMSPA, large, with letter-spacing (CSS letter-spacing: 2px)
		PromptText->SetFont(EclipseUI::MakeBMSPA(/*Size=*/28, /*LetterSpacingPx=*/2.f));
		PromptText->SetColorAndOpacity(FSlateColor(EclipseUI::Cyan));
		PromptText->SetJustification(ETextJustify::Center);

		// Glow text-shadow: rgba(81,238,252,0.5) at 8px blur — Slate single-shadow
		// approximation: cyan-tinted shadow with a small offset.
		PromptText->SetShadowOffset(FVector2D(0.f, 2.f));
		PromptText->SetShadowColorAndOpacity(FLinearColor(0.318f, 0.933f, 0.988f, 0.55f));
	}

	return Super::Initialize();
}

void UEclipseInteractWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Start hidden
	SetVisibility(ESlateVisibility::Hidden);

	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->OnNearTalkableChanged.AddDynamic(this, &UEclipseInteractWidget::HandleNearTalkableChanged);
		IS->OnNearItemChanged.AddDynamic(this, &UEclipseInteractWidget::HandleNearItemChanged);
		UE_LOG(LogEclipse, Log, TEXT("InteractWidget: bound to InteractSubsystem"));
	}
	else
	{
		UE_LOG(LogEclipse, Warning, TEXT("InteractWidget: failed to find InteractSubsystem"));
	}
}

void UEclipseInteractWidget::NativeDestruct()
{
	if (UEclipseInteractSubsystem* IS = GetWorld() ? GetWorld()->GetSubsystem<UEclipseInteractSubsystem>() : nullptr)
	{
		IS->OnNearTalkableChanged.RemoveDynamic(this, &UEclipseInteractWidget::HandleNearTalkableChanged);
		IS->OnNearItemChanged.RemoveDynamic(this, &UEclipseInteractWidget::HandleNearItemChanged);
	}
	Super::NativeDestruct();
}

void UEclipseInteractWidget::HandleNearTalkableChanged(AEclipseNpcCharacter* Npc)
{
	UE_LOG(LogEclipse, Log, TEXT("InteractWidget::HandleNearTalkableChanged → %s"),
		Npc ? *Npc->NpcName.ToString() : TEXT("(null)"));
	CachedNpc = Npc;
	RefreshPrompt();
}

void UEclipseInteractWidget::HandleNearItemChanged(AActor* Item)
{
	CachedItem = Item;
	RefreshPrompt();
}

void UEclipseInteractWidget::RefreshPrompt()
{
	if (!PromptText) return;

	// Talkable NPC takes priority over item
	if (CachedNpc.IsValid())
	{
		const FString Label = FString::Printf(TEXT("[E]  TALK TO %s"), *CachedNpc->NpcName.ToString().ToUpper());
		PromptText->SetText(FText::FromString(Label));
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	if (CachedItem.IsValid())
	{
		FText ItemDisplay;
		if (AEclipseItemActor* IA = Cast<AEclipseItemActor>(CachedItem.Get()))
		{
			ItemDisplay = IA->DisplayName.IsEmpty()
				? FText::FromName(IA->ItemId)
				: IA->DisplayName;
		}
		else
		{
			ItemDisplay = FText::FromString(CachedItem->GetName().ToUpper());
		}

		PromptText->SetText(FText::Format(
			FText::FromString(TEXT("[E]  PICK UP {0}")),
			FText::FromString(ItemDisplay.ToString().ToUpper())));
		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	SetVisibility(ESlateVisibility::Hidden);
}
