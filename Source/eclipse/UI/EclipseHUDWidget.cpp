// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUDWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "EclipseDeathOverlayWidget.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
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
	// Deliberately compact: the meters are a reference readout, not the
	// focus, and the side-quest checklist now shares this column under them.
	constexpr float ColumnWidth = 440.f;   // total width shared by every row + bar
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
		OutLabelText->SetFont(MakeBMSPA(/*Size=*/17, /*Letter=*/2.f));
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
		OutValueText->SetFont(MakeBMSPA(/*Size=*/17, /*Letter=*/1.5f));
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
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));
		}

		// Stack this block in the parent column.
		ParentColumn->AddChild(Block);
	}
}

bool UEclipseHUDWidget::Initialize()
{
	using namespace EclipseUI;

	UE_LOG(LogEclipse, Warning, TEXT("HUD DBG: Initialize() begin — HeatBar=%s ThirstBar=%s WBP_RootName=%s"),
		HeatBar        ? TEXT("BOUND") : TEXT("null"),
		ThirstBar      ? TEXT("BOUND") : TEXT("null"),
		(WidgetTree && WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("<none>"));

	// Bail if the WBP has already filled in the bars — designer content
	// takes priority and we don't want to double-build.
	if (WidgetTree && !HeatBar && !ThirstBar)
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

		// Heat + Thirst only — Stimulation was removed from the game.
	}

	return Super::Initialize();
}

void UEclipseHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Currency still lives on the phone face; the clock is back on the HUD,
	// bottom-right and large, because time is now a resource the player
	// spends by talking (see UEclipseGameStateSubsystem::TickChapterClock)
	// and needs to be readable at a glance without opening the phone.
	if (CurrencyText) CurrencyText->SetVisibility(ESlateVisibility::Collapsed);

	// Inject the readout if neither the WBP nor the fallback tree supplied
	// one — same runtime-injection pattern the dialogue widget uses, so the
	// clock exists regardless of which path built this HUD.
	if (!ChapterClockText && WidgetTree)
	{
		if (UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget))
		{
			ChapterClockText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("ChapterClockText_Runtime"));
			if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(ChapterClockText))
			{
				// Bottom-left, anchored to the corner so it holds position at
				// any resolution. Alignment (0,1) pins the widget's own
				// bottom-left to that anchor, so the inset is a simple
				// offset rather than a size-dependent calculation.
				CS->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
				CS->SetAlignment(FVector2D(0.f, 1.f));
				CS->SetAutoSize(true);
				CS->SetPosition(FVector2D(ClockMargin, -ClockMargin));
			}
		}
	}
	if (ChapterClockText)
	{
		// Purple halo. Slate has no blur, so the glow is a font OUTLINE in
		// the halo colour (hugging the glyph edges) plus a ZERO-offset drop
		// shadow in the same hue — zero offset is deliberate: an offset
		// would read as a drop shadow, centred it just thickens the halo
		// evenly on all sides.
		{
			FSlateFontInfo ClockFont = EclipseUI::MakeBMSPA(ClockFontSize, /*Letter=*/2.f);
			ClockFont.OutlineSettings.OutlineSize = ClockGlowSize;
			ClockFont.OutlineSettings.OutlineColor = ClockGlowColor;
			ClockFont.OutlineSettings.bApplyOutlineToDropShadows = true;
			ChapterClockText->SetFont(ClockFont);
		}
		ChapterClockText->SetShadowOffset(FVector2D::ZeroVector);
		ChapterClockText->SetShadowColorAndOpacity(ClockGlowColor);
		ChapterClockText->SetColorAndOpacity(FSlateColor(EclipseUI::Cream));
		ChapterClockText->SetJustification(ETextJustify::Left);
		ChapterClockText->SetVisibility(ESlateVisibility::HitTestInvisible);
		UpdateChapterClock();
	}

	// Side-quest checklist, appended under the meter bars. Both the WBP
	// populator and the C++ fallback name that box "MeterColumn", so this
	// finds it either way; if neither built one, the checklist is simply
	// absent rather than landing somewhere arbitrary on the canvas.
	if (!QuestList && WidgetTree)
	{
		if (UPanelWidget* MeterCol = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("MeterColumn"))))
		{
			QuestList = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("QuestList_Runtime"));
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(MeterCol->AddChild(QuestList)))
			{
				VS->SetPadding(FMargin(0.f, 10.f, 0.f, 0.f));
				VS->SetHorizontalAlignment(HAlign_Left);
			}
			UpdateQuestList();
		}
	}

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

	QuestRefreshCountdown -= DeltaSeconds;
	if (QuestRefreshCountdown <= 0.f)
	{
		QuestRefreshCountdown = QuestRefreshInterval;
		UpdateQuestList();
	}

	if (bDebugVisible)
	{
		DebugRefreshCountdown -= DeltaSeconds;
		if (DebugRefreshCountdown <= 0.f)
		{
			DebugRefreshCountdown = DebugRefreshInterval;
			UpdateDebugOverlay();
		}
	}

	// Per-meter pulse decay. Only repaint when at least one pulse is
	// still alive — the static bar state doesn't change per frame.
	const bool bAnyAlive =
		HeatPulse > 0.f || ThirstPulse > 0.f;
	if (!bAnyAlive) return;

	HeatPulse        = FMath::Max(0.f, HeatPulse        - DeltaSeconds);
	ThirstPulse      = FMath::Max(0.f, ThirstPulse      - DeltaSeconds);

	// Repaint with updated pulse values. UpdateBars will not re-trigger
	// pulses because the Last* values are already current.
	UpdateBars();
}

void UEclipseHUDWidget::HandleStateChanged()
{
	UpdateBars();
	// The clock only moves in discrete steps now (a dialogue choice bumps
	// it and broadcasts), so this delegate is its ONLY refresh path — there
	// is no per-frame update to fall back on.
	UpdateChapterClock();
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

	// Per-meter base tints. Critical-zone bars override to red via
	// ApplyBarStyle so designers don't have to pick a separate critical
	// colour.
	const FLinearColor HeatFill  (1.00f, 1.00f, 1.00f, 1.f);  // white
	const FLinearColor ThirstFill(0.00f, 0.40f, 0.80f, 1.f);  // hyperlink blue (#0066cc)

	// Pulse value passed to ApplyBarStyle: 0..1, peaking at full timer.
	const float HeatPulseAlpha = HeatPulse        / PulseDuration;
	const float ThirstPulseAlpha = ThirstPulse    / PulseDuration;

	ApplyBarStyle(HeatBar,   GS->Heat,   HeatFill,   HeatPulseAlpha,   /*bHighIsCritical=*/false);
	ApplyBarStyle(ThirstBar, GS->Thirst, ThirstFill, ThirstPulseAlpha, /*bHighIsCritical=*/true);

	const int32 Max = UEclipseGameStateSubsystem::MeterMax;
	if (HeatValueText)        HeatValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), GS->Heat, Max)));
	if (ThirstValueText)      ThirstValueText->SetText(FText::FromString(FString::Printf(TEXT("%d/%d"), GS->Thirst, Max)));
}

void UEclipseHUDWidget::ApplyBarStyle(UProgressBar* Bar, int32 Value, FLinearColor BaseTint, float Pulse, bool bHighIsCritical) const
{
	if (!Bar) return;

	const FLinearColor CriticalTint(0.95f, 0.18f, 0.18f, 1.f);
	// Heat passes bHighIsCritical=false: only 0 kills, so a hot player is
	// fine and shouldn't read as an alarm. (It also starts at 8, which is
	// exactly MeterCriticalHigh — without this the bar would be red from
	// the first frame of a new game.) Thirst keeps both ends critical.
	const bool bCritical =
		(Value <= UEclipseGameStateSubsystem::MeterCriticalLow) ||
		(bHighIsCritical && Value >= UEclipseGameStateSubsystem::MeterCriticalHigh);

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
		// Time only — the chapter label was dropped from this readout when
		// the clock moved to a large corner display; the phone face still
		// carries "CH N" for players who want it.
		const FText NewClock = GS->GetChapterClockText();
		const FString NewClockStr = NewClock.ToString();

		// Tick on the visible flick-over only. GetChapterClockText() floors
		// to ClockDisplayStepMinutes, so at 2 minutes per choice and a
		// 10-minute step this fires once every 5 choices. The
		// LastClockDisplay guard matters because OnStateChanged also fires
		// for meter changes — without it every stat tweak would tick.
		const bool bFlickedOver = !LastClockDisplay.IsEmpty() && LastClockDisplay != NewClockStr;
		LastClockDisplay = NewClockStr;

		ChapterClockText->SetText(NewClock);

		if (bFlickedOver)
		{
			if (UEclipseAudioSubsystem* Audio = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
			{
				Audio->PlayUI(ClockTickSound);
			}
		}
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

void UEclipseHUDWidget::UpdateQuestList()
{
	if (!QuestList) return;

	UGameInstance* GI = GetGameInstance();
	UEclipseDialogueSubsystem* DS = GI ? GI->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr;
	if (!DS) return;

	const TArray<FString> Lines = DS->GetActiveSideQuests();
	if (Lines == LastQuestLines) return;   // nothing gained or completed
	LastQuestLines = Lines;

	QuestList->ClearChildren();
	QuestList->SetVisibility(Lines.Num() > 0
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed);

	for (const FString& Line : Lines)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		T->SetText(FText::FromString(TEXT("- ") + Line));
		T->SetFont(EclipseUI::MakeRodin(QuestFontSize));
		T->SetColorAndOpacity(FSlateColor(EclipseUI::CreamDim));
		T->SetAutoWrapText(true);
		T->SetWrapTextAt(ColumnWidth);
		if (UVerticalBoxSlot* VS = QuestList->AddChildToVerticalBox(T))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
		}
	}
}

// ── Debug overlay (0 key) ───────────────────────────────────────────────

void UEclipseHUDWidget::EnsureDebugWidgets()
{
	if (DebugPanel || !WidgetTree) return;
	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!RootCanvas) return;

	// Translucent black slab, terminal style. Auto-sized around the columns
	// and pinned to the bottom-left corner, so it grows upward/rightward as
	// content changes instead of clipping.
	DebugPanel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DebugPanel_Runtime"));
	{
		FSlateBrush Slab;
		Slab.DrawAs = ESlateBrushDrawType::Image;
		Slab.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.78f));
		DebugPanel->SetBrush(Slab);
		DebugPanel->SetPadding(FMargin(14.f, 10.f));
	}
	if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(DebugPanel))
	{
		CS->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));
		CS->SetAlignment(FVector2D(0.f, 1.f));
		CS->SetAutoSize(true);
		CS->SetPosition(FVector2D(16.f, -16.f));
	}

	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("DebugColumns_Runtime"));
	DebugPanel->SetContent(Row);

	DebugColumns.Reset();
	for (int32 i = 0; i < DC_Count; ++i)
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), *FString::Printf(TEXT("DebugColumn_%d"), i));
		T->SetFont(EclipseUI::MakeRodin(11));
		T->SetColorAndOpacity(FSlateColor(EclipseUI::Cream));
		// Fixed column width keeps the columns aligned as values change
		// length; the slab is already opaque enough that no text outline
		// is needed for legibility.
		T->SetMinDesiredWidth(DebugColumnWidth);
		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(T))
		{
			HS->SetPadding(FMargin(0.f, 0.f, i == DC_Count - 1 ? 0.f : 18.f, 0.f));
			HS->SetVerticalAlignment(VAlign_Top);
		}
		DebugColumns.Add(T);
	}
}

void UEclipseHUDWidget::ToggleDebugOverlay()
{
	bDebugVisible = !bDebugVisible;

	// Built on first use rather than in NativeConstruct — no reason for a
	// release session to carry the widgets at all if 0 is never pressed.
	if (bDebugVisible) EnsureDebugWidgets();

	if (DebugPanel)
	{
		DebugPanel->SetVisibility(bDebugVisible
			? ESlateVisibility::HitTestInvisible
			: ESlateVisibility::Collapsed);
	}
	if (bDebugVisible)
	{
		DebugRefreshCountdown = DebugRefreshInterval;
		UpdateDebugOverlay();
	}
}

void UEclipseHUDWidget::UpdateDebugOverlay()
{
	if (DebugColumns.Num() != DC_Count) return;

	UGameInstance* GI = GetGameInstance();
	UEclipseGameStateSubsystem* GS = GI ? GI->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;

	TArray<FString> Col[DC_Count];
	Col[DC_Meters]   .Add(TEXT("[ METERS / TIME ]"));
	Col[DC_Stats]    .Add(TEXT("[ STATS ]"));
	Col[DC_Quest]    .Add(TEXT("[ QUEST / MONEY ]"));
	Col[DC_Inventory].Add(TEXT("[ INVENTORY ]"));
	Col[DC_Ink]      .Add(TEXT("[ INK GLOBALS ]"));

	if (!GS)
	{
		Col[DC_Meters].Add(TEXT("no game state"));
	}
	else
	{
		auto B = [](bool v) { return v ? TEXT("true") : TEXT("false"); };
		auto JoinNames = [](const TArray<FName>& A)
		{
			if (A.Num() == 0) return FString(TEXT("  (empty)"));
			TArray<FString> S;
			for (const FName& N : A) S.Add(TEXT("  ") + N.ToString());
			return FString::Join(S, TEXT("\n"));
		};

		TArray<FString>& M = Col[DC_Meters];
		M.Add(FString::Printf(TEXT("heat            %d / %d"), GS->Heat,   UEclipseGameStateSubsystem::MeterMax));
		M.Add(FString::Printf(TEXT("thirst          %d / %d"), GS->Thirst, UEclipseGameStateSubsystem::MeterMax));
		M.Add(FString::Printf(TEXT("heat_penalty    %s"), B(GS->bMaxHeatThirstPenaltyApplied)));
		M.Add(TEXT(""));
		M.Add(FString::Printf(TEXT("clock           %s"), *GS->GetChapterClockText().ToString()));
		M.Add(FString::Printf(TEXT("chapter         %d"), GS->Chapter));
		M.Add(FString::Printf(TEXT("elapsed         %.1fs"), GS->ChapterElapsedSeconds));
		M.Add(FString::Printf(TEXT("last_heat_decay %.1fs"), GS->LastHeatDecayAtSeconds));
		M.Add(FString::Printf(TEXT("running         %s"), B(GS->bClockRunning)));

		TArray<FString>& St = Col[DC_Stats];
		St.Add(FString::Printf(TEXT("aesthetics      %d  (xp %d)"), GS->Aesthetics,   GS->AestheticsXP));
		St.Add(FString::Printf(TEXT("rhythm          %d  (xp %d)"), GS->Rhythm,       GS->RhythmXP));
		St.Add(FString::Printf(TEXT("zen             %d  (xp %d)"), GS->Zen,          GS->ZenXP));
		St.Add(FString::Printf(TEXT("psychedelics    %d  (xp %d)"), GS->Psychedelics, GS->PsychedelicsXP));
		St.Add(FString::Printf(TEXT("xp per level    %d"), UEclipseGameStateSubsystem::StatXPToLevel));
		St.Add(TEXT(""));
		St.Add(TEXT("-- hidden --"));
		St.Add(FString::Printf(TEXT("gender          %s"), *GS->Gender.ToString()));
		St.Add(FString::Printf(TEXT("race            %s"), *GS->Race.ToString()));
		St.Add(FString::Printf(TEXT("annoyance       %d / %d"), GS->Annoyance,
			UEclipseGameStateSubsystem::AnnoyanceMax));

		TArray<FString>& Q = Col[DC_Quest];
		Q.Add(FString::Printf(TEXT("stage           %s"), *GS->Quest.Stage.ToString()));
		Q.Add(FString::Printf(TEXT("has_hair        %s"), B(GS->Quest.bHasHair)));
		Q.Add(FString::Printf(TEXT("has_eye         %s"), B(GS->Quest.bHasEye)));
		Q.Add(FString::Printf(TEXT("has_wristband   %s"), B(GS->bHasWristband)));
		Q.Add(FString::Printf(TEXT("vip_access      %s"), B(GS->bVipAccessGranted)));
		Q.Add(FString::Printf(TEXT("met_npcs        %d"), GS->MetNPCs.Num()));
		Q.Add(FString::Printf(TEXT("failed_choices  %d"), GS->FailedChoicesThisChapter.Num()));
		Q.Add(TEXT(""));
		Q.Add(FString::Printf(TEXT("coins           %d"), GS->Coins));
		Q.Add(FString::Printf(TEXT("notes           %d"), GS->Notes));

		TArray<FString>& Inv = Col[DC_Inventory];
		Inv.Add(FString::Printf(TEXT("character       %s"), *GS->SelectedCharacterId.ToString()));
		Inv.Add(TEXT(""));
		Inv.Add(FString::Printf(TEXT("items (%d)"), GS->Inventory.Num()));
		Inv.Add(JoinNames(GS->Inventory));
		Inv.Add(TEXT(""));
		Inv.Add(FString::Printf(TEXT("clothing (%d)"), GS->EquippedClothing.Num()));
		Inv.Add(JoinNames(GS->EquippedClothing));
	}

	if (UEclipseDialogueSubsystem* DS = GI ? GI->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr)
	{
		const TArray<FString> Ink = DS->GetInkVariableDump();
		if (Ink.Num() == 0) Col[DC_Ink].Add(TEXT("(story not loaded)"));
		else                Col[DC_Ink].Append(Ink);
	}

	for (int32 i = 0; i < DC_Count; ++i)
	{
		DebugColumns[i]->SetText(FText::FromString(FString::Join(Col[i], TEXT("\n"))));
	}
}
