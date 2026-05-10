// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUDWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
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

		// (Centre crosshair removed — pointer-driven interactions don't need a
		// reticle. The CrosshairImage UPROPERTY is kept so any WBP that still
		// references it via BindWidgetOptional doesn't break the build.)

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

		// HudBg holds a vertical column: [inventory ribbon] above [portrait+meters row].
		UVerticalBox* HudColumn = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("HudColumn"));
		HudBg->SetContent(HudColumn);

		// ── Chapter clock readout — top of the cluster, right-aligned. ──
		// Format: "CH 1 · 1:23". Updated every frame in NativeTick.
		ChapterClockText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("ChapterClockText"));
		ChapterClockText->SetText(FText::FromString(TEXT("CH 0 · 0:00")));
		ChapterClockText->SetFont(MakeBMSPA(13, 3.f));
		ChapterClockText->SetColorAndOpacity(FSlateColor(Cyan));
		if (UVerticalBoxSlot* VS = HudColumn->AddChildToVerticalBox(ChapterClockText))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
			VS->SetHorizontalAlignment(HAlign_Right);
		}

		// Inner horizontal layout: portrait | heat-cluster | thirst-cluster
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), TEXT("HudRow"));
		HudColumn->AddChildToVerticalBox(Row);

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

	// ── Runtime injection of ChapterClockText if the WBP didn't ship one. ──
	// Mounts the clock readout at the top of the HUD column. Designer can
	// still bake a designer-styled ChapterClockText into the WBP later —
	// when present, BindWidgetOptional resolves and this block skips.
	if (!ChapterClockText && WidgetTree)
	{
		using namespace EclipseUI;

		// Find the HudColumn that holds the rest of the HUD — populator
		// bakes it as "HudColumn".
		UVerticalBox* HudColumn = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("HudColumn")));

		if (HudColumn)
		{
			ChapterClockText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("ChapterClockText_Runtime"));
			ChapterClockText->SetText(FText::FromString(TEXT("CH 0  ·  0:00")));
			ChapterClockText->SetFont(MakeBMSPA(13, 3.f));
			ChapterClockText->SetColorAndOpacity(FSlateColor(Cyan));

			HudColumn->AddChild(ChapterClockText);
			HudColumn->ShiftChild(0, ChapterClockText);   // top of the column

			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ChapterClockText->Slot))
			{
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
				VS->SetHorizontalAlignment(HAlign_Right);
			}

			UE_LOG(LogEclipse, Log, TEXT("HUD: ChapterClockText injected at runtime (parent=%s)"),
				*HudColumn->GetName());
		}
		else
		{
			UE_LOG(LogEclipse, Warning, TEXT("HUD: no HudColumn found — ChapterClockText not injected"));
		}
	}

	// ── Runtime injection of CurrencyText if the WBP didn't ship one. ──
	// Mounts a small "◆ N  ▤ M" line in the HUD column above the meters.
	if (!CurrencyText && WidgetTree)
	{
		using namespace EclipseUI;
		UVerticalBox* HudColumn = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("HudColumn")));
		if (HudColumn)
		{
			CurrencyText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("CurrencyText_Runtime"));
			CurrencyText->SetText(FText::FromString(TEXT("◆ 0   ▤ 0")));
			CurrencyText->SetFont(MakeBMSPA(12, 2.f));
			CurrencyText->SetColorAndOpacity(FSlateColor(Cream));

			HudColumn->AddChild(CurrencyText);
			// Slot below ChapterClockText (index 1 if clock is at 0).
			const int32 ClockIdx = ChapterClockText
				? HudColumn->GetChildIndex(ChapterClockText) : -1;
			HudColumn->ShiftChild(FMath::Max(0, ClockIdx + 1), CurrencyText);

			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(CurrencyText->Slot))
			{
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
				VS->SetHorizontalAlignment(HAlign_Right);
			}
			UE_LOG(LogEclipse, Log, TEXT("HUD: CurrencyText injected at runtime"));
		}
	}

	if (UEclipseGameStateSubsystem* GS = GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseHUDWidget::HandleStateChanged);
	}

	UpdateBars();
	UpdateCurrency();
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
	UpdateCurrency();
}

void UEclipseHUDWidget::UpdateCurrency()
{
	if (!CurrencyText) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;
	CurrencyText->SetText(FText::FromString(
		FString::Printf(TEXT("◆ %d   ▤ %d"), GS->Coins, GS->Notes)));
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

void UEclipseHUDWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);
	UpdateChapterClock();
}

void UEclipseHUDWidget::UpdateChapterClock()
{
	if (!ChapterClockText) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	// Time-of-day readout — H:MM, 24-hour, starting at 0:00 (midnight) and
	// counting up. Each in-game *minute* on the clock advances when the
	// underlying ChapterElapsedSeconds accumulator crosses another 60s of
	// game-time. Wraps every 24 hours so the display stays a real clock
	// rather than drifting to "25:00" after a long session.
	const float Elapsed     = FMath::Max(0.f, GS->ChapterElapsedSeconds);
	const int32 TotalMins   = FMath::FloorToInt(Elapsed / 60.f);
	const int32 HourOfDay   = (TotalMins / 60) % 24;
	const int32 MinOfHour   = TotalMins % 60;
	ChapterClockText->SetText(FText::FromString(
		FString::Printf(TEXT("CH %d  ·  %d:%02d"), GS->Chapter, HourOfDay, MinOfHour)));
}
