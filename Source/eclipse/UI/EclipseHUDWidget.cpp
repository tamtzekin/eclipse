// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUDWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Subsystems/EclipseGameStateSubsystem.h"

// ─────────────────────────────────────────────────────────────
//  HUD layout (mirrors HTML #hud-status, bottom-right cluster)
//
//  ┌────────────────────────────────────┐  ← rgba(6,14,36,0.6)
//  │                                    │     1px #1a3a5c
//  │  ┌────────┐   HEAT      THIRST     │     padding 10/12
//  │  │ PORTRAIT│  ▓▓▓        ▓▓▓        │     gap 12
//  │  │  90x112 │  ▓▓▓        ▓▓▓        │
//  │  │  cyan   │  ▓▓▓        ▓▓▓        │
//  │  └────────┘  ▓▓▓        ▓▓▓        │
//  └────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────

bool UEclipseHUDWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("HeatBar"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// ── Crosshair (build first so it's drawn behind everything else) ────
		CrosshairImage = WidgetTree->ConstructWidget<UImage>(
			UImage::StaticClass(), TEXT("CrosshairImage"));
		FSlateBrush CrosshairBrush;
		CrosshairBrush.DrawAs       = ESlateBrushDrawType::RoundedBox;
		CrosshairBrush.ImageSize    = FVector2D(12.f, 12.f);
		CrosshairBrush.TintColor    = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		CrosshairBrush.OutlineSettings.CornerRadii  = FVector4(6.f, 6.f, 6.f, 6.f);
		CrosshairBrush.OutlineSettings.Color        = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.6f));
		CrosshairBrush.OutlineSettings.Width        = 1.5f;
		CrosshairBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		CrosshairImage->SetBrush(CrosshairBrush);
		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(CrosshairImage))
		{
			CSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			CSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CSlot->SetSize(FVector2D(12.f, 12.f));
		}

		// ── HUD container — bottom-right ────────────────────────────────────
		// CSS: position:fixed; bottom:20; right:20; bg:rgba(6,14,36,0.6);
		//      border:1px #1a3a5c; padding:10/12; display:flex; gap:12
		UBorder* HudBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("HudBg"));
		HudBg->SetBrush(RoundedBrush(PanelBg, PanelBorder, /*Outline=*/1.f, /*Radius=*/0.f));
		HudBg->SetPadding(FMargin(12.f, 10.f));

		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(HudBg))
		{
			CSlot->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
			CSlot->SetAlignment(FVector2D(1.f, 1.f));
			CSlot->SetAutoSize(true);
			CSlot->SetPosition(FVector2D(-20.f, -20.f));
		}

		// Inner horizontal layout: portrait | heat-cluster | thirst-cluster
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("HudRow"));
		HudBg->SetContent(Row);

		// ── Portrait (90×112, cyan border, gradient bg) ──
		UBorder* PortraitFrame = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("PortraitFrame"));
		FSlateBrush PortraitBrush;
		PortraitBrush.DrawAs    = ESlateBrushDrawType::Box;
		// CSS background: linear-gradient(135deg, #0a1a3a 0%, #1a3a6a 100%)
		// Slate has no gradient brush by default; pick the midpoint as a flat color.
		PortraitBrush.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
		PortraitBrush.OutlineSettings.Color = FSlateColor(Cyan);
		PortraitBrush.OutlineSettings.Width = 2.f;
		PortraitBrush.OutlineSettings.CornerRadii = FVector4(0,0,0,0);
		PortraitBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		PortraitFrame->SetBrush(PortraitBrush);

		// Wrap the portrait Border in a SizeBox so it gets a real 90×112 footprint.
		USizeBox* PortraitSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("PortraitSize"));
		PortraitSize->SetWidthOverride(90.f);
		PortraitSize->SetHeightOverride(112.f);
		PortraitSize->AddChild(PortraitFrame);

		if (UHorizontalBoxSlot* CSlot = Row->AddChildToHorizontalBox(PortraitSize))
		{
			CSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			CSlot->SetVerticalAlignment(VAlign_Center);
		}

		// Portrait label until art is in
		UTextBlock* PortraitLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("PortraitLabel"));
		PortraitLabel->SetText(FText::FromString(TEXT("YOU")));
		PortraitLabel->SetFont(MakeBMSPA(14, 4.f));
		PortraitLabel->SetColorAndOpacity(FSlateColor(Cyan));
		PortraitLabel->SetJustification(ETextJustify::Center);
		PortraitFrame->SetContent(PortraitLabel);
		PortraitFrame->SetHorizontalAlignment(HAlign_Center);
		PortraitFrame->SetVerticalAlignment(VAlign_Center);

		// ── Heat cluster (label above bar) ──
		UVerticalBox* HeatCluster = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("HeatCluster"));
		if (UHorizontalBoxSlot* CSlot = Row->AddChildToHorizontalBox(HeatCluster))
		{
			CSlot->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			CSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		UTextBlock* HeatLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("HeatLabel"));
		HeatLabel->SetText(FText::FromString(TEXT("HEAT")));
		HeatLabel->SetFont(MakeBMSPA(14, 4.f));
		HeatLabel->SetColorAndOpacity(FSlateColor(LabelHeat));
		if (UVerticalBoxSlot* S = HeatCluster->AddChildToVerticalBox(HeatLabel))
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		// Heat bar — 16×112, dark bg, blue fill that lerps to cyan
		HeatBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("HeatBar"));
		{
			FProgressBarStyle S;
			S.BackgroundImage = SolidBrush(BarTrack);
			S.FillImage       = SolidBrush(HeatLow);
			HeatBar->SetWidgetStyle(S);
		}
		HeatBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		HeatBar->SetPercent(0.6f);
		USizeBox* HeatSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("HeatSize"));
		HeatSize->SetWidthOverride(16.f);
		HeatSize->SetHeightOverride(112.f);
		HeatSize->AddChild(HeatBar);
		HeatCluster->AddChildToVerticalBox(HeatSize);

		// ── Thirst cluster ──
		UVerticalBox* ThirstCluster = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("ThirstCluster"));
		if (UHorizontalBoxSlot* CSlot = Row->AddChildToHorizontalBox(ThirstCluster))
		{
			CSlot->SetVerticalAlignment(VAlign_Bottom);
		}

		UTextBlock* ThirstLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ThirstLabel"));
		ThirstLabel->SetText(FText::FromString(TEXT("THIRST")));
		ThirstLabel->SetFont(MakeBMSPA(14, 4.f));
		ThirstLabel->SetColorAndOpacity(FSlateColor(LabelThirst));
		if (UVerticalBoxSlot* S = ThirstCluster->AddChildToVerticalBox(ThirstLabel))
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		ThirstBar = WidgetTree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(), TEXT("ThirstBar"));
		{
			FProgressBarStyle S;
			S.BackgroundImage = SolidBrush(BarTrack);
			S.FillImage       = SolidBrush(Cyan);
			ThirstBar->SetWidgetStyle(S);
		}
		ThirstBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		ThirstBar->SetPercent(0.8f);
		USizeBox* ThirstSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("ThirstSize"));
		ThirstSize->SetWidthOverride(16.f);
		ThirstSize->SetHeightOverride(112.f);
		ThirstSize->AddChild(ThirstBar);
		ThirstCluster->AddChildToVerticalBox(ThirstSize);
	}

	return Super::Initialize();
}

void UEclipseHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UEclipseGameStateSubsystem* GS = GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseHUDWidget::HandleStateChanged);
	}

	UpdateBars();
}

void UEclipseHUDWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			GS->OnStateChanged.RemoveDynamic(this, &UEclipseHUDWidget::HandleStateChanged);
	}
	Super::NativeDestruct();
}

void UEclipseHUDWidget::HandleStateChanged()
{
	UpdateBars();
}

void UEclipseHUDWidget::UpdateBars()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>();
	if (!GS) return;

	const float HeatPct   = GS->MaxHeat   > 0.f ? GS->Heat   / GS->MaxHeat   : 0.f;
	const float ThirstPct = GS->MaxThirst > 0.f ? GS->Thirst / GS->MaxThirst : 0.f;

	if (HeatBar)
	{
		HeatBar->SetPercent(FMath::Clamp(HeatPct, 0.f, 1.f));
		// Fill color gradient: HeatLow (#143c8c) at 0% → HeatHigh (cyan) at 100%
		HeatBar->SetFillColorAndOpacity(FLinearColor::LerpUsingHSV(
			EclipseUI::HeatLow, EclipseUI::HeatHigh, HeatPct));
	}

	if (ThirstBar)
	{
		ThirstBar->SetPercent(FMath::Clamp(ThirstPct, 0.f, 1.f));
		ThirstBar->SetFillColorAndOpacity(EclipseUI::Cyan);
	}
}
