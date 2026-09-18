// Copyright (c) ECLIPSE. All Rights Reserved.

#include "UI/EclipseDanceBattleWidget.h"
#include "EclipseUiStyle.h"
#include "Data/EclipseDanceTrackData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

// ── Waveform strip ──

void UEclipseWaveformWidget::SetTrack(UEclipseDanceTrackData* InTrack, const TArray<TPair<float, EEclipseDanceStyle>>& InSwitches)
{
	Track = InTrack;
	Switches = InSwitches;
}

int32 UEclipseWaveformWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!Track || Track->Envelope.Num() == 0) return LayerId;

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float MidY = Size.Y * 0.5f;
	const float PxPerSec = Size.X / WindowSeconds;
	const float Start = Playhead - WindowSeconds * 0.5f;
	const float Rate = Track->EnvelopeRate;
	const auto Line = [&](float X, float Y0, float Y1, const FLinearColor& C, float Thick)
	{
		TArray<FVector2D> Pts{ FVector2D(X, Y0), FVector2D(X, Y1) };
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, Thick);
	};

	constexpr float ColumnPx = 3.f;
	for (float X = 0.f; X < Size.X; X += ColumnPx)
	{
		const float T = Start + X / PxPerSec;
		const int32 I = FMath::FloorToInt(T * Rate);
		if (!Track->Envelope.IsValidIndex(I)) continue;
		const float H = FMath::Max(1.f, Track->Envelope[I] * MidY);
		// Played audio dims, like a deck's already-passed waveform.
		const float A = X < Size.X * 0.5f ? 0.18f : 0.38f;
		Line(X, MidY - H, MidY + H, FLinearColor(1.f, 1.f, 1.f, A), ColumnPx - 1.f);
	}

	// Bar lines, then switch markers in the incoming style's colour.
	const float Bar = Track->BarSeconds();
	for (float T = Track->FirstDownbeatSeconds + FMath::CeilToFloat((Start - Track->FirstDownbeatSeconds) / Bar) * Bar; T < Start + WindowSeconds; T += Bar)
	{
		Line((T - Start) * PxPerSec, 0.f, Size.Y, FLinearColor(1.f, 1.f, 1.f, 0.12f), 1.f);
	}
	for (const TPair<float, EEclipseDanceStyle>& S : Switches)
	{
		const float X = (S.Key - Start) * PxPerSec;
		if (X < 0.f || X > Size.X) continue;
		FLinearColor C = EclipseDance::StyleColor(S.Value);
		C.A = 0.8f;
		Line(X, 0.f, Size.Y, C, 3.f);
	}
	Line(Size.X * 0.5f, 0.f, Size.Y, FLinearColor(1.f, 1.f, 1.f, 0.6f), 2.f);   // playhead
	return LayerId + 1;
}

// ── Style wheel ──

void UEclipseStyleWheelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	SelectPunch = FMath::Max(0.f, SelectPunch - InDeltaTime * 5.f);
}

int32 UEclipseStyleWheelWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D C = Size * 0.5f;
	const float R = FMath::Min(Size.X, Size.Y) * 0.5f;
	const float Inner = R * 0.34f, Outer = R * 0.62f, Mid = (Inner + Outer) * 0.5f;
	constexpr float SlotSpan = 45.f, Gap = 3.f;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();

	const auto Polar = [&](float Deg, float Radius) { return C + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg)), -FMath::Sin(FMath::DegreesToRadians(Deg))) * Radius; };
	const auto Arc = [&](float FromDeg, float ToDeg, float Radius, float Thick, const FLinearColor& Col, int32 Layer)
	{
		TArray<FVector2D> Pts;
		for (int32 i = 0; i <= 16; ++i) Pts.Add(Polar(FMath::Lerp(FromDeg, ToDeg, i / 16.f), Radius));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Pts, ESlateDrawEffect::None, Col, true, Thick);
	};
	const auto Label = [&](const FString& Str, const FSlateFontInfo& Font, FVector2D At, const FLinearColor& Col, int32 Layer)
	{
		const FVector2D TextSize = Measure->Measure(Str, Font);
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
			AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At - TextSize * 0.5f)), Str, Font, ESlateDrawEffect::None, Col);
	};

	// Dark backing ring, then every slot, then the selected one lifted out of the ring.
	Arc(0.f, 360.f, Mid, Outer - Inner + 14.f, FLinearColor(0.f, 0.f, 0.f, 0.55f), LayerId + 1);
	const FSlateFontInfo NameFont = EclipseUI::MakeBMSPA(12, 1.f);
	const FSlateFontInfo ComboFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
	{
		const EEclipseDanceStyle S = (EEclipseDanceStyle)i;
		const EclipseDance::FStyleInfo& Info = EclipseDance::Info(S);
		const bool bSel = S == Selected;
		FLinearColor Col = EclipseDance::StyleColor(S);
		Col.A = bSel ? 0.95f : 0.28f;
		const float Lift = bSel ? 6.f + 6.f * SelectPunch : 0.f;
		Arc(Info.WheelDeg - SlotSpan * 0.5f + Gap, Info.WheelDeg + SlotSpan * 0.5f - Gap, Mid + Lift, Outer - Inner + (bSel ? 8.f : 0.f), Col, LayerId + 2);
		Label(Info.Combo, ComboFont, Polar(Info.WheelDeg, Mid + Lift), bSel ? FLinearColor::Black : FLinearColor(1.f, 1.f, 1.f, 0.7f), LayerId + 3);
		FLinearColor NameCol = EclipseDance::StyleColor(S);
		NameCol.A = bSel ? 1.f : 0.55f;
		Label(Info.Name, NameFont, Polar(Info.WheelDeg, Outer + 22.f), NameCol, LayerId + 3);
	}
	if (Selected != EEclipseDanceStyle::Count)
	{
		Label(EclipseDance::Info(Selected).Name, EclipseUI::MakeBMSPA(15, 2.f), C, EclipseDance::StyleColor(Selected), LayerId + 3);
	}
	return LayerId + 3;
}

// ── Battle readout ──

bool UEclipseDanceBattleWidget::Initialize()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("StyleText"))))
	{
		BuildTree(WidgetTree);
	}
	return Super::Initialize();
}

void UEclipseDanceBattleWidget::BuildTree(UWidgetTree* Tree)
{
	using namespace EclipseUI;

	UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	Tree->RootWidget = Root;

	auto Fullscreen = [Root](UWidget* W)
	{
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(W))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}
	};
	// Darken the room around the dancers, then a style-coloured edge glow that flashes on the beat.
	UImage* Vignette = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Vignette"));
	FSlateBrush Dark = InnerGlowBrush(/*EdgePx=*/420.f);
	Dark.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Vignette->SetBrush(Dark);
	Vignette->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fullscreen(Vignette);
	UImage* Pulse = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StylePulse"));
	FSlateBrush Glow = InnerGlowBrush(/*EdgePx=*/220.f);
	Glow.TintColor = FSlateColor(FLinearColor::White);   // colour comes from Pulse()
	Pulse->SetBrush(Glow);
	Pulse->SetRenderOpacity(0.f);
	Pulse->SetVisibility(ESlateVisibility::HitTestInvisible);
	Fullscreen(Pulse);

	auto Text = [&](FName Name, int32 Size, float Letter, const FString& Str = FString()) -> UTextBlock*
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo F = MakeBMSPA(Size, Letter);
		F.OutlineSettings.OutlineSize = 2;
		F.OutlineSettings.OutlineColor = FLinearColor::Black;
		T->SetFont(F);
		T->SetJustification(ETextJustify::Center);
		T->SetText(FText::FromString(Str));
		return T;
	};
	auto AddCentred = [](UVerticalBox* Box, UWidget* W)
	{
		if (UVerticalBoxSlot* S = Box->AddChildToVerticalBox(W)) S->SetHorizontalAlignment(HAlign_Center);
	};

	UVerticalBox* Col = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DanceColumn"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Col))
	{
		S->SetAnchors(FAnchors(0.5f, 0.f, 0.5f, 0.f));
		S->SetAlignment(FVector2D(0.5f, 0.f));
		S->SetPosition(FVector2D(0.f, 40.f));
		S->SetAutoSize(true);
	}

	UTextBlock* Style = Text(TEXT("StyleText"), 64, 6.f);
	Style->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	AddCentred(Col, Style);

	// Waveform sits behind the countdown, faded, so it reads as a backdrop.
	UOverlay* Deck = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CountdownDeck"));
	AddCentred(Col, Deck);
	USizeBox* WaveSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("WaveformSize"));
	WaveSize->SetWidthOverride(720.f);
	WaveSize->SetHeightOverride(110.f);
	WaveSize->SetContent(Tree->ConstructWidget<UEclipseWaveformWidget>(UEclipseWaveformWidget::StaticClass(), TEXT("Waveform")));
	if (UOverlaySlot* S = Deck->AddChildToOverlay(WaveSize))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* Countdown = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CountdownBox"));
	Countdown->SetVisibility(ESlateVisibility::Hidden);
	if (UOverlaySlot* S = Deck->AddChildToOverlay(Countdown))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBox* NextRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("NextRow"));
	AddCentred(Countdown, NextRow);
	NextRow->AddChildToHorizontalBox(Text(TEXT("NextLabel"), 26, 3.f, TEXT("NEXT:")));
	if (UHorizontalBoxSlot* S = NextRow->AddChildToHorizontalBox(Text(TEXT("NextStyle"), 26, 3.f)))
	{
		S->SetPadding(FMargin(12.f, 0.f, 0.f, 0.f));
	}
	UTextBlock* Number = Text(TEXT("CountNumber"), 44, 0.f);
	Number->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	AddCentred(Countdown, Number);

	AddCentred(Col, Text(TEXT("GradeText"), 30, 4.f));

	USizeBox* WheelSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("StyleWheelSize"));
	WheelSize->SetWidthOverride(300.f);
	WheelSize->SetHeightOverride(300.f);
	WheelSize->SetContent(Tree->ConstructWidget<UEclipseStyleWheelWidget>(UEclipseStyleWheelWidget::StaticClass(), TEXT("StyleWheel")));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(WheelSize))
	{
		S->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		S->SetAlignment(FVector2D(1.f, 1.f));
		S->SetPosition(FVector2D(-40.f, -40.f));
		S->SetAutoSize(true);
	}

	// Results board, centre screen; rows are added at the end of the battle.
	UBorder* Results = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ResultsPanel"));
	Results->SetBrush(RoundedBrush(FLinearColor(0.f, 0.f, 0.f, 0.85f), DialogueRed, 2.f, 0.f));
	Results->SetPadding(FMargin(36.f, 24.f));
	Results->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Results))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetAutoSize(true);
	}
	UVerticalBox* ResultsCol = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResultsColumn"));
	Results->SetContent(ResultsCol);
	USizeBox* RowsWidth = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ResultsWidth"));
	RowsWidth->SetWidthOverride(420.f);
	RowsWidth->SetContent(Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ResultsBox")));
	ResultsCol->AddChildToVerticalBox(RowsWidth);
	UTextBlock* Verdict = Text(TEXT("VerdictText"), 40, 5.f);
	Verdict->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	if (UVerticalBoxSlot* S = ResultsCol->AddChildToVerticalBox(Verdict))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
	}
}

void UEclipseDanceBattleWidget::ShowStyle(EEclipseDanceStyle Style)
{
	if (!StyleText) return;
	StyleText->SetText(Style == EEclipseDanceStyle::Count ? FText::GetEmpty() : EclipseDance::StyleName(Style));
	StyleText->SetColorAndOpacity(FSlateColor(EclipseDance::StyleColor(Style)));
	StylePunch = 1.f;
}

void UEclipseDanceBattleWidget::ShowCountdown(int32 Count, EEclipseDanceStyle Next, float BeatSeconds)
{
	if (!CountdownBox || !NextStyle || !CountNumber) return;
	if (Count <= 0)
	{
		CountdownBox->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	const FLinearColor NextColor = EclipseDance::StyleColor(Next);
	NextStyle->SetText(EclipseDance::StyleName(Next));
	NextStyle->SetColorAndOpacity(FSlateColor(NextColor));
	NextStyle->SetVisibility(bShowNames ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (NextLabel) NextLabel->SetColorAndOpacity(FSlateColor(bShowNames ? FLinearColor::White : NextColor));
	CountNumber->SetText(FText::AsNumber(Count));
	CountdownBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	CountBeatSeconds = BeatSeconds;
	CountPunch = Count <= 3 ? 1.f : 0.f;
}

void UEclipseDanceBattleWidget::ShowGrade(const FText& Grade, const FLinearColor& Color)
{
	if (!GradeText) return;
	GradeText->SetText(Grade);
	GradeText->SetColorAndOpacity(FSlateColor(Color));
	GradeFade = 1.f;
}

void UEclipseDanceBattleWidget::SetRadialSelected(EEclipseDanceStyle Style)
{
	if (StyleWheel) StyleWheel->SetSelected(Style);
}

void UEclipseDanceBattleWidget::Pulse(const FLinearColor& Color, float BeatSeconds)
{
	if (!StylePulse) return;
	StylePulse->SetColorAndOpacity(Color);
	PulseA = 1.f;
	PulseBeatSeconds = FMath::Max(0.1f, BeatSeconds);
}

float UEclipseDanceBattleWidget::ShowResults(const TArray<TPair<FString, int32>>& Rows, const FString& Verdict, const FLinearColor& VerdictColor)
{
	constexpr float RowSeconds = 0.55f;
	if (!ResultsPanel || !ResultsBox) return 0.f;

	ResultsBox->ClearChildren();
	ResultRows.Reset();
	for (const TPair<FString, int32>& R : Rows)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const FSlateFontInfo Font = EclipseUI::MakeBMSPA(22, 2.f);
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Name->SetFont(Font);
		Name->SetText(FText::FromString(R.Key));
		UTextBlock* Value = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Value->SetFont(Font);
		Value->SetJustification(ETextJustify::Right);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Name)) S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Row->AddChildToHorizontalBox(Value);
		Row->SetVisibility(ESlateVisibility::Hidden);   // keeps its space so the board doesn't grow as it fills
		if (UVerticalBoxSlot* S = ResultsBox->AddChildToVerticalBox(Row)) S->SetPadding(FMargin(0.f, 3.f));
		ResultRows.Add({ Value, R.Value, -1 });
	}
	if (VerdictText)
	{
		VerdictText->SetText(FText::FromString(Verdict));
		VerdictText->SetColorAndOpacity(FSlateColor(VerdictColor));
		VerdictText->SetVisibility(ESlateVisibility::Hidden);
	}
	ResultsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
	ResultsClock = 0.f;
	return ResultRows.Num() * RowSeconds + 0.4f;
}

void UEclipseDanceBattleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (StylePulse)
	{
		PulseA = FMath::Max(0.f, PulseA - InDeltaTime / PulseBeatSeconds);
		StylePulse->SetRenderOpacity(0.55f * PulseA * PulseA);
	}
	if (ResultsClock >= 0.f)
	{
		constexpr float RowSeconds = 0.55f, CountSeconds = 0.4f;
		ResultsClock += InDeltaTime;
		for (int32 i = 0; i < ResultRows.Num(); ++i)
		{
			FResultRow& R = ResultRows[i];
			const float T = ResultsClock - i * RowSeconds;
			if (T < 0.f || !R.Value) continue;
			if (R.Shown < 0)
			{
				R.Value->GetParent()->SetVisibility(ESlateVisibility::HitTestInvisible);
				if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
				{
					Audio->PlayCue(EEclipseUiCue::DialogueLine, 0.6f);
				}
			}
			const int32 Now = FMath::RoundToInt(R.Target * FMath::Clamp(T / CountSeconds, 0.f, 1.f));
			if (Now != R.Shown)
			{
				R.Shown = Now;
				R.Value->SetText(FText::AsNumber(Now));
			}
		}
		if (VerdictText && ResultsClock >= ResultRows.Num() * RowSeconds + 0.2f && !VerdictText->IsVisible())
		{
			VerdictText->SetVisibility(ESlateVisibility::HitTestInvisible);
			VerdictPunch = 1.f;
		}
		VerdictPunch = FMath::Max(0.f, VerdictPunch - InDeltaTime * 3.f);
		if (VerdictText) VerdictText->SetRenderScale(FVector2D(1.f + 0.5f * VerdictPunch * VerdictPunch));
	}
	if (StyleText && StylePunch > 0.f)
	{
		StylePunch = FMath::Max(0.f, StylePunch - InDeltaTime * 4.f);
		StyleText->SetRenderScale(FVector2D(1.f + 0.3f * StylePunch * StylePunch));
	}
	if (CountNumber)
	{
		// Pulse-fade: swells and brightens on the beat, shrinks and dims across it.
		CountPunch = FMath::Max(0.f, CountPunch - InDeltaTime / FMath::Max(0.1f, CountBeatSeconds));
		CountNumber->SetRenderScale(FVector2D(1.f + 0.6f * CountPunch * CountPunch));
		CountNumber->SetRenderOpacity(CountPunch > 0.f ? 0.45f + 0.55f * CountPunch : 1.f);
	}
	if (GradeText)
	{
		GradeFade = FMath::Max(0.f, GradeFade - InDeltaTime * 0.8f);
		GradeText->SetRenderOpacity(FMath::Min(1.f, GradeFade * 2.f));
	}
}
