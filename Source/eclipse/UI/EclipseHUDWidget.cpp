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

// ─────────────────────────────────────────────────────────────────────────────
//  HUD — top-left cluster of three life-meters. Designer-styled via
//  WBP_HUD; the C++ fallback tree below kicks in when the WBP has no
//  segment rows yet so the widget always shows something.
//
//  Each row:  [LABEL]  [10 segments + 2 dotted dividers]  [VALUE]
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Per-meter visual tunables — pulled out so the populator + the runtime
	// fallback agree on dimensions.
	constexpr float SegmentWidth   = 22.f;
	constexpr float SegmentHeight  = 22.f;
	constexpr float DividerWidth   = 2.f;
	constexpr float LabelWidth     = 110.f;   // reserved width for "STIMULATION"
	constexpr float ValueWidth     = 28.f;    // single/double-digit numbers

	// Build one bar (label + outline-framed segment row + value label) and
	// slot it into the supplied parent UVerticalBox so all three rows
	// stack cleanly. Returns the inner segment row + the outer frame
	// UBorder + the value/label text blocks through out-params so the
	// HUD widget can keep them for TintBar / TintFrame / value-text
	// updates.
	void BuildOneBar(UWidgetTree* Tree, UPanelWidget* ParentColumn,
		const TCHAR* Suffix, const FString& LabelStr,
		FLinearColor DividerTint,
		UHorizontalBox*& OutSegmentRow,
		UBorder*& OutBarFrame,
		UTextBlock*& OutLabelText,
		UTextBlock*& OutValueText)
	{
		using namespace EclipseUI;

		// Outer row: [Label] [Segments] [Value]
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sRow"), Suffix)));

		// Label (left side).
		OutLabelText = Tree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sLabelText"), Suffix)));
		OutLabelText->SetText(FText::FromString(LabelStr));
		OutLabelText->SetFont(MakeBMSPA(/*Size=*/14, /*Letter=*/3.f));
		OutLabelText->SetColorAndOpacity(FSlateColor(Cream));
		USizeBox* LabelSize = Tree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sLabelSize"), Suffix)));
		LabelSize->SetWidthOverride(LabelWidth);
		LabelSize->AddChild(OutLabelText);
		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(LabelSize))
		{
			HS->SetVerticalAlignment(VAlign_Center);
			HS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		// Segment row (middle).
		OutSegmentRow = Tree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sSegmentRow"), Suffix)));

		// Segments left→right: index 0 = leftmost, index 9 = rightmost.
		// Insert dotted-line dividers between segments 1-2 (critical-low
		// boundary) and 7-8 (critical-high boundary).
		for (int32 i = 0; i < UEclipseGameStateSubsystem::MeterMax; ++i)
		{
			UBorder* Seg = Tree->ConstructWidget<UBorder>(
				UBorder::StaticClass(),
				FName(*FString::Printf(TEXT("%sSeg_%d"), Suffix, i)));
			Seg->SetBrush(SolidBrush(FLinearColor(0.08f, 0.10f, 0.14f, 0.85f)));
			Seg->SetPadding(FMargin(0.f));
			USizeBox* SegSize = Tree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(),
				FName(*FString::Printf(TEXT("%sSegSize_%d"), Suffix, i)));
			SegSize->SetWidthOverride(SegmentWidth);
			SegSize->SetHeightOverride(SegmentHeight);
			SegSize->AddChild(Seg);
			OutSegmentRow->AddChildToHorizontalBox(SegSize);

			// Divider AFTER this segment when the next segment would cross
			// a critical boundary. With CritLow=2 the boundary sits
			// between seg 1 and seg 2 → insert after i==1. With CritHigh=8
			// the boundary sits between seg 7 and seg 8 → insert after i==7.
			const bool bDividerAfter =
				(i == UEclipseGameStateSubsystem::MeterCriticalLow - 1) ||      // i==1
				(i == UEclipseGameStateSubsystem::MeterCriticalHigh - 1);       // i==7
			if (bDividerAfter)
			{
				UBorder* Div = Tree->ConstructWidget<UBorder>(
					UBorder::StaticClass(),
					FName(*FString::Printf(TEXT("%sDivider_%d"), Suffix, i)));
				Div->SetBrush(SolidBrush(DividerTint));
				USizeBox* DivSize = Tree->ConstructWidget<USizeBox>(
					USizeBox::StaticClass(),
					FName(*FString::Printf(TEXT("%sDivSize_%d"), Suffix, i)));
				DivSize->SetWidthOverride(DividerWidth);
				DivSize->SetHeightOverride(SegmentHeight);
				DivSize->AddChild(Div);
				OutSegmentRow->AddChildToHorizontalBox(DivSize);
			}
		}

		// Wrap the segment row in an outer frame so we can glow the
		// outline during the pulse. Brush is RoundedBox with transparent
		// fill + faint cream outline at rest.
		OutBarFrame = Tree->ConstructWidget<UBorder>(
			UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("%sBarFrame"), Suffix)));
		{
			FSlateBrush B;
			B.DrawAs    = ESlateBrushDrawType::RoundedBox;
			B.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
			B.OutlineSettings.Color        = FSlateColor(FLinearColor(0.945f, 0.929f, 0.851f, 0.35f));
			B.OutlineSettings.Width        = 1.f;
			B.OutlineSettings.CornerRadii  = FVector4(2.f, 2.f, 2.f, 2.f);
			B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			OutBarFrame->SetBrush(B);
		}
		OutBarFrame->SetPadding(FMargin(2.f));
		OutBarFrame->SetContent(OutSegmentRow);

		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(OutBarFrame))
		{
			HS->SetVerticalAlignment(VAlign_Center);
			HS->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		// Value (right side).
		OutValueText = Tree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%sValueText"), Suffix)));
		OutValueText->SetText(FText::FromString(TEXT("0")));
		OutValueText->SetFont(MakeBMSPA(/*Size=*/16, /*Letter=*/2.f));
		OutValueText->SetColorAndOpacity(FSlateColor(Cream));
		OutValueText->SetJustification(ETextJustify::Right);
		USizeBox* ValSize = Tree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("%sValueSize"), Suffix)));
		ValSize->SetWidthOverride(ValueWidth);
		ValSize->AddChild(OutValueText);
		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(ValSize))
		{
			HS->SetVerticalAlignment(VAlign_Center);
		}

		// Stack this row in the parent column.
		if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(ParentColumn->AddChild(Row)))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		}
	}
}

bool UEclipseHUDWidget::Initialize()
{
	using namespace EclipseUI;

	UE_LOG(LogEclipse, Warning, TEXT("HUD DBG: Initialize() begin — HeatRow=%s ThirstRow=%s StimRow=%s WBP_RootName=%s"),
		HeatSegmentRow        ? TEXT("BOUND") : TEXT("null"),
		ThirstSegmentRow      ? TEXT("BOUND") : TEXT("null"),
		StimulationSegmentRow ? TEXT("BOUND") : TEXT("null"),
		(WidgetTree && WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("<none>"));

	// Bail if the WBP has already filled in the segment rows — designer
	// content takes priority and we don't want to double-build.
	if (WidgetTree && !HeatSegmentRow && !ThirstSegmentRow && !StimulationSegmentRow)
	{
		UE_LOG(LogEclipse, Warning, TEXT("HUD DBG: building C++ fallback tree"));

		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// Top-left container — navy panel, cream-chalk outline.
		UBorder* HudBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("HudBg"));
		HudBg->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
		HudBg->SetPadding(FMargin(14.f, 12.f));
		if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(HudBg))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CS->SetAlignment(FVector2D(0.f, 0.f));
			CS->SetAutoSize(true);
			CS->SetPosition(FVector2D(20.f, 20.f));
		}

		UVerticalBox* MeterCol = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("MeterColumn"));
		HudBg->SetContent(MeterCol);

		const FLinearColor DivTint(0.945f, 0.929f, 0.851f, 0.55f);

		UHorizontalBox* TmpRow = nullptr;
		UBorder*        TmpFrame = nullptr;
		UTextBlock*     TmpLabel = nullptr;
		UTextBlock*     TmpValue = nullptr;
		BuildOneBar(WidgetTree, MeterCol, TEXT("Heat"),        TEXT("HEAT"),        DivTint, TmpRow, TmpFrame, TmpLabel, TmpValue);
		HeatSegmentRow = TmpRow; HeatBarFrame = TmpFrame; HeatLabelText = TmpLabel; HeatValueText = TmpValue;

		BuildOneBar(WidgetTree, MeterCol, TEXT("Thirst"),      TEXT("THIRST"),      DivTint, TmpRow, TmpFrame, TmpLabel, TmpValue);
		ThirstSegmentRow = TmpRow; ThirstBarFrame = TmpFrame; ThirstLabelText = TmpLabel; ThirstValueText = TmpValue;

		BuildOneBar(WidgetTree, MeterCol, TEXT("Stimulation"), TEXT("STIMULATION"), DivTint, TmpRow, TmpFrame, TmpLabel, TmpValue);
		StimulationSegmentRow = TmpRow; StimulationBarFrame = TmpFrame; StimulationLabelText = TmpLabel; StimulationValueText = TmpValue;
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

	// Per-meter base tints. Critical-zone segments override to red via
	// TintBar so designers don't have to pick a separate critical colour.
	const FLinearColor HeatFill (0.95f, 0.30f, 0.20f, 1.f);   // red
	const FLinearColor ThirstFill(Cyan);                     // cyan
	const FLinearColor StimFill (0.98f, 0.95f, 0.62f, 1.f);  // yellow-white

	// Pulse value passed to TintBar: 0..1, peaking at full timer.
	const float HeatPulseAlpha = HeatPulse        / PulseDuration;
	const float ThirstPulseAlpha = ThirstPulse    / PulseDuration;
	const float StimPulseAlpha = StimulationPulse / PulseDuration;

	TintBar(HeatSegmentRow,        GS->Heat,        HeatFill,  HeatPulseAlpha);
	TintBar(ThirstSegmentRow,      GS->Thirst,      ThirstFill, ThirstPulseAlpha);
	TintBar(StimulationSegmentRow, GS->Stimulation, StimFill,   StimPulseAlpha);

	TintFrame(HeatBarFrame,        HeatPulseAlpha);
	TintFrame(ThirstBarFrame,      ThirstPulseAlpha);
	TintFrame(StimulationBarFrame, StimPulseAlpha);

	if (HeatValueText)        HeatValueText->SetText(FText::AsNumber(GS->Heat));
	if (ThirstValueText)      ThirstValueText->SetText(FText::AsNumber(GS->Thirst));
	if (StimulationValueText) StimulationValueText->SetText(FText::AsNumber(GS->Stimulation));
}

void UEclipseHUDWidget::TintBar(UHorizontalBox* SegmentRow, int32 Value, FLinearColor BaseTint, float Pulse) const
{
	using namespace EclipseUI;
	if (!SegmentRow) return;

	const FLinearColor TrackTint(0.08f, 0.10f, 0.14f, 0.85f);
	const FLinearColor CriticalTint(0.95f, 0.18f, 0.18f, 1.f);

	// Pulse strength: at Pulse=1 the lit segments are lerped ~40% toward
	// white. Decays linearly with the pulse timer in UpdateBars/NativeTick.
	const float FlashAmount = FMath::Clamp(Pulse, 0.f, 1.f) * 0.4f;

	for (int32 ChildIdx = 0; ChildIdx < SegmentRow->GetChildrenCount(); ++ChildIdx)
	{
		UWidget* Child = SegmentRow->GetChildAt(ChildIdx);
		if (!Child) continue;

		// Segments are wrapped in USizeBox containers — recover the inner
		// UBorder for tinting. Dividers are USizeBox containers too, but
		// their name pattern is "<Prefix>DivSize_*" which we skip via the
		// SegSize_ name filter below.
		UBorder* Seg = nullptr;
		const FString CName = Child->GetName();
		if (USizeBox* SizeBox = Cast<USizeBox>(Child))
		{
			Seg = Cast<UBorder>(SizeBox->GetChildAt(0));
		}
		if (!Seg) continue;

		// Identify segment index from the parent USizeBox's name pattern
		// "<Prefix>SegSize_<index>" — anything else (dividers) is skipped.
		const int32 UnderscoreIdx = CName.Find(TEXT("SegSize_"));
		if (UnderscoreIdx == INDEX_NONE) continue;
		const FString NumStr = CName.Mid(UnderscoreIdx + 8); // "SegSize_" = 8 chars
		const int32 Index = FCString::Atoi(*NumStr);

		// Lit if the meter value covers this segment (segment[i] lights up
		// when Value > i). Critical zones use red regardless of BaseTint.
		const bool bLit = (Value > Index);
		const bool bIsCriticalZoneIndex =
			(Index <  UEclipseGameStateSubsystem::MeterCriticalLow) ||
			(Index >= UEclipseGameStateSubsystem::MeterCriticalHigh);

		FLinearColor T = TrackTint;
		if (bLit)
		{
			const FLinearColor LitColor = bIsCriticalZoneIndex ? CriticalTint : BaseTint;
			// Per-index gradient: dimmer toward the left edge of the bar,
			// brighter toward the right — so as the meter fills more
			// segments, the bar reads as "saturating" smoothly. The dim
			// floor is 55% of the lit colour; index 0 sits at the floor,
			// index 9 sits at the full lit colour.
			const float GradientT = (UEclipseGameStateSubsystem::MeterMax > 1)
				? (float)Index / (float)(UEclipseGameStateSubsystem::MeterMax - 1)
				: 1.f;
			const FLinearColor DimLit = LitColor * 0.55f;
			T = FMath::Lerp(DimLit, LitColor, GradientT);
			T.A = LitColor.A;   // keep full alpha after multiplicative dim

			// Lerp lit segments toward white during the pulse for a quick
			// "just changed" flash. Track (unlit) segments are unaffected
			// so the change reads as the fill itself flashing.
			if (FlashAmount > 0.f)
			{
				T = FMath::Lerp(T, FLinearColor::White, FlashAmount);
			}
		}
		Seg->SetBrush(SolidBrush(T));
	}
}

void UEclipseHUDWidget::TintFrame(UBorder* BarFrame, float Pulse) const
{
	if (!BarFrame) return;

	// Resting outline = faint cream chalk; pulse lerps it toward bright
	// white for the duration of the flash. Width also nudges up slightly
	// at peak so the glow reads as "thicker" while active.
	const FLinearColor RestingOutline(0.945f, 0.929f, 0.851f, 0.35f);
	const FLinearColor GlowOutline   (1.f,    1.f,    1.f,    1.f);
	const float Alpha = FMath::Clamp(Pulse, 0.f, 1.f);

	FSlateBrush B;
	B.DrawAs    = ESlateBrushDrawType::RoundedBox;
	B.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));   // transparent fill
	B.OutlineSettings.Color        = FSlateColor(FMath::Lerp(RestingOutline, GlowOutline, Alpha));
	B.OutlineSettings.Width        = FMath::Lerp(1.f, 2.f, Alpha);
	B.OutlineSettings.CornerRadii  = FVector4(2.f, 2.f, 2.f, 2.f);
	B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	BarFrame->SetBrush(B);
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
