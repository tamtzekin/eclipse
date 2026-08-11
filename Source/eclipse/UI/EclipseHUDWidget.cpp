// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUDWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "EclipseDeathOverlayWidget.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ProgressBar.h"

// ─────────────────────────────────────────────────────────────────────────────
//  HUD — top-left cluster of three life-meters. Designer-styled via
//  WBP_HUD; the C++ fallback tree below kicks in when the WBP has no bars
//  yet so the widget always shows something. No backdrop panel — bars
//  float directly over the game view.
//
//  Per meter (two lines):  [LABEL] .......... [VALUE/MAX]
//                          [============== bar ==============]
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Per-meter visual tunables — pulled out so the populator + the runtime
	// fallback agree on dimensions.
	constexpr float ColumnWidth = 320.f;   // total width shared by every row + bar
	constexpr float BarHeight   = 14.f;

	// Build one meter block (label+value row, then a full-width fill bar)
	// and slot it into the supplied parent UVerticalBox. Returns the bar +
	// label/value text blocks through out-params so the HUD widget can
	// keep them for ApplyBarStyle / value-text updates.
	void BuildOneBar(UWidgetTree* Tree, UPanelWidget* ParentColumn,
		const TCHAR* Suffix, const FString& LabelStr,
		UProgressBar*& OutBar,
		UTextBlock*& OutLabelText,
		UTextBlock*& OutValueText)
	{
		using namespace EclipseUI;

		UVerticalBox* Block = Tree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sBlock"), Suffix)));

		// ── Top line: [Label] .......... [Value/Max] ──
		UHorizontalBox* TopRow = Tree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sRow"), Suffix)));

		OutLabelText = Tree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sLabelText"), Suffix)));
		OutLabelText->SetText(FText::FromString(LabelStr));
		OutLabelText->SetFont(MakeBMSPA(/*Size=*/18, /*Letter=*/3.f));
		OutLabelText->SetColorAndOpacity(FSlateColor(Cream));
		if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(OutLabelText))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetVerticalAlignment(VAlign_Bottom);
		}

		OutValueText = Tree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sValueText"), Suffix)));
		OutValueText->SetText(FText::FromString(FString::Printf(TEXT("0/%d"), UEclipseGameStateSubsystem::MeterMax)));
		OutValueText->SetFont(MakeBMSPA(/*Size=*/18, /*Letter=*/2.f));
		OutValueText->SetColorAndOpacity(FSlateColor(Cream));
		OutValueText->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(OutValueText))
		{
			HS->SetVerticalAlignment(VAlign_Bottom);
		}

		if (UVerticalBoxSlot* VS = Block->AddChildToVerticalBox(TopRow))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}

		// ── Bottom line: full-width flat fill bar, no frame/track glow ──
		OutBar = Tree->ConstructWidget<UProgressBar>(
			UProgressBar::StaticClass(),
			FName(*FString::Printf(TEXT("%sBar"), Suffix)));
		{
			FProgressBarStyle Style;
			Style.BackgroundImage = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.3f));
			Style.FillImage       = SolidBrush(FLinearColor::White); // tinted per-frame via SetFillColorAndOpacity
			OutBar->SetWidgetStyle(Style);
		}
		OutBar->SetPercent(0.f);

		USizeBox* BarSize = Tree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sBarSize"), Suffix)));
		BarSize->SetHeightOverride(BarHeight);
		BarSize->AddChild(OutBar);
		if (UVerticalBoxSlot* VS = Block->AddChildToVerticalBox(BarSize))
		{
			VS->SetHorizontalAlignment(HAlign_Fill);
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// Stack this block in the parent column.
		ParentColumn->AddChild(Block);
	}
}

bool UEclipseHUDWidget::Initialize()
{
	using namespace EclipseUI;

	UE_LOG(LogEclipse, Warning, TEXT("HUD DBG: Initialize() begin — HeatBar=%s ThirstBar=%s StimBar=%s WBP_RootName=%s"),
		HeatBar        ? TEXT("BOUND") : TEXT("null"),
		ThirstBar      ? TEXT("BOUND") : TEXT("null"),
		StimulationBar ? TEXT("BOUND") : TEXT("null"),
		(WidgetTree && WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("<none>"));

	// Bail if the WBP has already filled in the bars — designer content
	// takes priority and we don't want to double-build.
	if (WidgetTree && !HeatBar && !ThirstBar && !StimulationBar)
	{
		UE_LOG(LogEclipse, Warning, TEXT("HUD DBG: building C++ fallback tree"));

		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// No backdrop — bars sit directly on the canvas, top-left, wrapped
		// only in a fixed-width SizeBox so the Fill-aligned rows/bars below
		// have a concrete width to stretch to.
		USizeBox* ColumnSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("MeterColumnSize"));
		ColumnSize->SetWidthOverride(ColumnWidth);
		if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(ColumnSize))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetAutoSize(true);
			CS->SetPosition(FVector2D(20.f, 20.f));
		}

		UVerticalBox* MeterCol = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("MeterColumn"));
		ColumnSize->AddChild(MeterCol);

		UProgressBar* TmpBar = nullptr;
		UTextBlock*   TmpLabel = nullptr;
		UTextBlock*   TmpValue = nullptr;
		BuildOneBar(WidgetTree, MeterCol, TEXT("Heat"),        TEXT("HEAT"),        TmpBar, TmpLabel, TmpValue);
		HeatBar = TmpBar; HeatLabelText = TmpLabel; HeatValueText = TmpValue;

		BuildOneBar(WidgetTree, MeterCol, TEXT("Thirst"),      TEXT("THIRST"),      TmpBar, TmpLabel, TmpValue);
		ThirstBar = TmpBar; ThirstLabelText = TmpLabel; ThirstValueText = TmpValue;

		BuildOneBar(WidgetTree, MeterCol, TEXT("Stimulation"), TEXT("STIM"),        TmpBar, TmpLabel, TmpValue);
		StimulationBar = TmpBar; StimulationLabelText = TmpLabel; StimulationValueText = TmpValue;
	}

	return Super::Initialize();
}

void UEclipseHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Phone-face owns the clock + currency readouts now. Leave the bound
	// HUD widgets alive but collapsed so a future design can flip them
	// back on with a single SetVisibility(Visible).
	if (ChapterClockText) ChapterClockText->SetVisibility(ESlateVisibility::Collapsed);
	if (CurrencyText)     CurrencyText->SetVisibility(ESlateVisibility::Collapsed);

	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseHUDWidget::HandleStateChanged);
		GS->OnPlayerDeath.AddDynamic(this, &UEclipseHUDWidget::HandlePlayerDeath);
	}

	UpdateBars();
}

void UEclipseHUDWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnStateChanged.RemoveDynamic(this, &UEclipseHUDWidget::HandleStateChanged);
			GS->OnPlayerDeath.RemoveDynamic(this, &UEclipseHUDWidget::HandlePlayerDeath);
		}
	}
	Super::NativeDestruct();
}

void UEclipseHUDWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);

	// Per-meter pulse decay. Only repaint when at least one pulse is
	// still alive — the static bar state doesn't change per frame.
	const bool bAnyAlive =
		HeatPulse > 0.f || ThirstPulse > 0.f || StimulationPulse > 0.f;
	if (!bAnyAlive) return;

	HeatPulse        = FMath::Max(0.f, HeatPulse        - DeltaSeconds);
	ThirstPulse      = FMath::Max(0.f, ThirstPulse      - DeltaSeconds);
	StimulationPulse = FMath::Max(0.f, StimulationPulse - DeltaSeconds);

	// Repaint with updated pulse values. UpdateBars will not re-trigger
	// pulses because the Last* values are already current.
	UpdateBars();
}

void UEclipseHUDWidget::HandleStateChanged()
{
	UpdateBars();
}

void UEclipseHUDWidget::HandlePlayerDeath()
{
	UEclipseDeathOverlayWidget::OpenForPlayer(GetOwningPlayer());
}

void UEclipseHUDWidget::UpdateBars()
{
	using namespace EclipseUI;

	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	// Detect changes since the last broadcast — kick the corresponding
	// pulse timer to full so the bar flashes briefly on the next frames.
	// The first ever call seeds Last* without flashing (LastX == -1
	// sentinel ensures no spurious pulse on game start).
	auto DetectChange = [](int32 NewV, int32& LastV, float& PulseOut)
	{
		if (LastV != -1 && NewV != LastV)
		{
			PulseOut = UEclipseHUDWidget::PulseDuration;
		}
		LastV = NewV;
	};
	DetectChange(GS->Heat,        LastHeat,        HeatPulse);
	DetectChange(GS->Thirst,      LastThirst,      ThirstPulse);
	DetectChange(GS->Stimulation, LastStimulation, StimulationPulse);

	// Per-meter base tints. Critical-zone bars override to red via
	// ApplyBarStyle so designers don't have to pick a separate critical
	// colour.
	const FLinearColor HeatFill (0.95f, 0.30f, 0.20f, 1.f);   // red
	const FLinearColor ThirstFill(Cyan);                     // cyan
	const FLinearColor StimFill (0.98f, 0.95f, 0.62f, 1.f);  // yellow-white

	// Pulse value passed to ApplyBarStyle: 0..1, peaking at full timer.
	const float HeatPulseAlpha = HeatPulse        / PulseDuration;
	const float ThirstPulseAlpha = ThirstPulse    / PulseDuration;
	const float StimPulseAlpha = StimulationPulse / PulseDuration;

	ApplyBarStyle(HeatBar,        GS->Heat,        HeatFill,   HeatPulseAlpha);
	ApplyBarStyle(ThirstBar,      GS->Thirst,      ThirstFill, ThirstPulseAlpha);
	ApplyBarStyle(StimulationBar, GS->Stimulation, StimFill,   StimPulseAlpha);

	const int32 Max = UEclipseGameStateSubsystem::MeterMax;
	if (HeatValueText)        HeatValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), GS->Heat, Max)));
	if (ThirstValueText)      ThirstValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), GS->Thirst, Max)));
	if (StimulationValueText) StimulationValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), GS->Stimulation, Max)));
}

void UEclipseHUDWidget::ApplyBarStyle(UProgressBar* Bar, int32 Value, FLinearColor BaseTint, float Pulse) const
{
	if (!Bar) return;

	const FLinearColor CriticalTint(0.95f, 0.18f, 0.18f, 1.f);
	const bool bCritical =
		(Value <= UEclipseGameStateSubsystem::MeterCriticalLow) ||
		(Value >= UEclipseGameStateSubsystem::MeterCriticalHigh);

	FLinearColor T = bCritical ? CriticalTint : BaseTint;

	// Lerp toward white during the pulse for a quick "just changed" flash.
	const float FlashAmount = FMath::Clamp(Pulse, 0.f, 1.f) * 0.4f;
	if (FlashAmount > 0.f)
	{
		T = FMath::Lerp(T, FLinearColor::White, FlashAmount);
	}

	Bar->SetPercent((float)Value / (float)UEclipseGameStateSubsystem::MeterMax);
	Bar->SetFillColorAndOpacity(T);
}

void UEclipseHUDWidget::UpdateChapterClock()
{
	if (!ChapterClockText) return;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		ChapterClockText->SetText(FText::FromString(FString::Printf(
			TEXT("%s  ·  %s"),
			*GS->GetChapterLabelText().ToString(),
			*GS->GetChapterClockText().ToString())));
	}
}

void UEclipseHUDWidget::UpdateCurrency()
{
	if (!CurrencyText) return;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		CurrencyText->SetText(FText::FromString(FString::Printf(
			TEXT("C %d   N %d"), GS->Coins, GS->Notes)));
	}
}
