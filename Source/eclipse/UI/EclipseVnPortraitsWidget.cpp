// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseVnPortraitsWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseDialogueSubsystem.h"

bool UEclipseVnPortraitsWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("NpcPortrait"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		auto BuildSlot = [&](const FName& BorderName, const FName& LabelName,
			const FName& SizeName, TObjectPtr<UBorder>& OutBorder, TObjectPtr<UTextBlock>& OutLabel,
			float Anchor, float OffsetX) -> void
		{
			OutBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), BorderName);
			FSlateBrush B;
			B.DrawAs    = ESlateBrushDrawType::Box;
			B.TintColor = FSlateColor(FLinearColor(0.024f, 0.055f, 0.141f, 0.85f));
			OutBorder->SetBrush(B);
			OutBorder->SetPadding(FMargin(0.f));
			OutBorder->SetHorizontalAlignment(HAlign_Center);
			OutBorder->SetVerticalAlignment(VAlign_Center);

			OutLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), LabelName);
			OutLabel->SetText(FText::FromString(TEXT("…")));
			OutLabel->SetFont(MakeBMSPA(20, 3.f));
			OutLabel->SetColorAndOpacity(FSlateColor(Cyan));
			OutLabel->SetJustification(ETextJustify::Center);
			OutBorder->SetContent(OutLabel);

			USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), SizeName);
			Size->SetWidthOverride(340.f);
			Size->SetHeightOverride(460.f);
			Size->AddChild(OutBorder);

			if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Size))
			{
				S->SetAnchors(FAnchors(Anchor, 1.f, Anchor, 1.f));
				S->SetAlignment(FVector2D(Anchor, 1.f));
				S->SetAutoSize(true);
				S->SetPosition(FVector2D(OffsetX, 0.f));
			}
		};

		// NPC on left, Player on right (right-edge minus dialogue panel width)
		BuildSlot(TEXT("NpcPortrait"),    TEXT("NpcLabel"),    TEXT("NpcSize"),
		          NpcPortrait,    NpcLabel,    /*Anchor=*/0.f, /*OffsetX=*/10.f);
		BuildSlot(TEXT("PlayerPortrait"), TEXT("PlayerLabel"), TEXT("PlayerSize"),
		          PlayerPortrait, PlayerLabel, /*Anchor=*/1.f, /*OffsetX=*/-400.f);
	}

	return Super::Initialize();
}

void UEclipseVnPortraitsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SetVisibility(ESlateVisibility::Hidden);

	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
	{
		DS->OnDialogueOpened.AddDynamic(this, &UEclipseVnPortraitsWidget::HandleOpened);
		DS->OnNodeChanged.AddDynamic(this, &UEclipseVnPortraitsWidget::HandleNodeChanged);
		DS->OnDialogueClosed.AddDynamic(this, &UEclipseVnPortraitsWidget::HandleClosed);
	}
}

void UEclipseVnPortraitsWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			DS->OnDialogueOpened.RemoveDynamic(this, &UEclipseVnPortraitsWidget::HandleOpened);
			DS->OnNodeChanged.RemoveDynamic(this, &UEclipseVnPortraitsWidget::HandleNodeChanged);
			DS->OnDialogueClosed.RemoveDynamic(this, &UEclipseVnPortraitsWidget::HandleClosed);
		}
	}
	Super::NativeDestruct();
}

void UEclipseVnPortraitsWidget::HandleOpened(AEclipseNpcCharacter* Npc)
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (NpcLabel && Npc)
	{
		NpcLabel->SetText(FText::FromName(Npc->NpcName));
	}
	if (PlayerLabel)
	{
		PlayerLabel->SetText(FText::FromString(TEXT("YOU")));
	}
	// NPC speaks first
	SetActive(NpcPortrait,    NpcLabel,    /*bActive=*/true);
	SetActive(PlayerPortrait, PlayerLabel, /*bActive=*/false);
}

void UEclipseVnPortraitsWidget::HandleNodeChanged(FEclipseDialogueNodeView Node)
{
	// Synthetic dialogue speakers are always the NPC (player choices are
	// merged into the NPC's choices array). For now, the NPC portrait stays
	// active during NPC speech, and we'll cross-fade once choice-driven
	// player speech bubbles are added.
	SetActive(NpcPortrait,    NpcLabel,    /*bActive=*/true);
	SetActive(PlayerPortrait, PlayerLabel, /*bActive=*/false);
}

void UEclipseVnPortraitsWidget::HandleClosed()
{
	SetVisibility(ESlateVisibility::Hidden);
}

void UEclipseVnPortraitsWidget::SetActive(UBorder* Frame, UTextBlock* Label, bool bActive)
{
	if (Frame)
	{
		Frame->SetBrushColor(bActive
			? FLinearColor(0.024f, 0.055f, 0.141f, 0.92f)
			: FLinearColor(0.024f, 0.055f, 0.141f, 0.45f));
	}
	if (Label)
	{
		Label->SetColorAndOpacity(FSlateColor(bActive
			? EclipseUI::Cyan
			: FLinearColor(EclipseUI::Cyan.R, EclipseUI::Cyan.G, EclipseUI::Cyan.B, 0.4f)));
	}
}
