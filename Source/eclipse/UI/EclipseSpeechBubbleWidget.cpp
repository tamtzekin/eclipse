// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseSpeechBubbleWidget.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"

bool UEclipseSpeechBubbleWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("BubbleBg"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// Bubble bg — pill shape (radius 16), cyan border, deep-blue gradient (flat avg)
		BubbleBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("BubbleBg"));
		FSlateBrush BgBrush;
		BgBrush.DrawAs    = ESlateBrushDrawType::RoundedBox;
		BgBrush.TintColor = FSlateColor(FLinearColor(0.090f, 0.224f, 0.471f, 1.f)); // avg of #1e4a96/#0f2c62
		BgBrush.OutlineSettings.CornerRadii  = FVector4(16, 16, 16, 16);
		BgBrush.OutlineSettings.Color        = FSlateColor(Cyan);
		BgBrush.OutlineSettings.Width        = 2.f;
		BgBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		BubbleBg->SetBrush(BgBrush);
		BubbleBg->SetPadding(FMargin(12.f, 4.f));
		BubbleBg->SetHorizontalAlignment(HAlign_Center);
		BubbleBg->SetVerticalAlignment(VAlign_Center);

		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("BubbleSize"));
		Size->SetMinDesiredWidth(34.f);
		Size->SetMinDesiredHeight(26.f);
		Size->AddChild(BubbleBg);

		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(Size))
		{
			CSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CSlot->SetAutoSize(true);
		}

		BubbleText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("BubbleText"));
		BubbleText->SetText(FText::FromString(TEXT("?")));
		BubbleText->SetFont(MakeRodin(/*Size=*/22));
		BubbleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.941f, 0.973f, 1.f, 1.f))); // #f0f8ff
		BubbleText->SetJustification(ETextJustify::Center);
		BubbleBg->SetContent(BubbleText);
	}

	return Super::Initialize();
}

void UEclipseSpeechBubbleWidget::SetBubble(EEclipseBubbleType InType, bool bMuted)
{
	if (!BubbleText) return;

	const TCHAR* Label = TEXT("?");
	switch (InType)
	{
		case EEclipseBubbleType::None:     SetVisibility(ESlateVisibility::Hidden); return;
		case EEclipseBubbleType::Question: Label = TEXT("?");  break;
		case EEclipseBubbleType::Exclaim:  Label = TEXT("!");  break;
		case EEclipseBubbleType::Both:     Label = TEXT("?!"); break;
		case EEclipseBubbleType::Muted:    Label = TEXT("…");  break;
	}
	BubbleText->SetText(FText::FromString(Label));
	SetVisibility(ESlateVisibility::HitTestInvisible);

	// Greyed style when player can't talk to the NPC (e.g. thirst=0)
	if (BubbleBg)
	{
		FSlateBrush B;
		B.DrawAs    = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(bMuted
			? FLinearColor(0.105f, 0.114f, 0.141f, 1.f)   // #1a1d24
			: FLinearColor(0.090f, 0.224f, 0.471f, 1.f));
		B.OutlineSettings.CornerRadii  = FVector4(16, 16, 16, 16);
		B.OutlineSettings.Width        = 2.f;
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		B.OutlineSettings.Color        = FSlateColor(bMuted
			? FLinearColor(0.353f, 0.384f, 0.451f, 1.f)   // #5a6273
			: EclipseUI::Cyan);
		BubbleBg->SetBrush(B);
		BubbleText->SetColorAndOpacity(FSlateColor(bMuted
			? FLinearColor(0.541f, 0.576f, 0.639f, 1.f)   // #8a93a3
			: FLinearColor(0.941f, 0.973f, 1.f, 1.f)));
	}
}
