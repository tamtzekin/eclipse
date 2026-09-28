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
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace
{
	void DrawLine(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, FVector2D A, FVector2D B, const FLinearColor& C, float Thick)
	{
		TArray<FVector2D> Pts{ A, B };
		FSlateDrawElement::MakeLines(Out, Layer, Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, Thick);
	}

	void DrawCentredText(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geo, const FString& Str, const FSlateFontInfo& Font, FVector2D At, const FLinearColor& Col)
	{
		const FVector2D Size = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, Font);
		FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(Size, FSlateLayoutTransform(At - Size * 0.5f)), Str, Font, ESlateDrawEffect::None, Col);
	}
}

// ── Waveform ──

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
	const TArray<float>& Env = Track->Envelope;
	const auto Sample = [&](const TArray<float>& A, float T)
	{
		// Interpolated between samples so it isn't stair-stepped, with only a touch of smoothing to keep the detail.
		const float F = T * Rate;
		const int32 I = FMath::FloorToInt(F);
		if (!A.IsValidIndex(I) || !A.IsValidIndex(I + 1)) return -1.f;
		const float Here = FMath::Lerp(A[I], A[I + 1], F - I);
		const float Around = 0.5f * (A[FMath::Max(0, I - 1)] + A[FMath::Min(A.Num() - 1, I + 2)]);
		return FMath::Lerp(Here, Around, 0.1f);
	};
	const bool bBands = Track->EnvelopeLow.Num() == Env.Num() && Track->EnvelopeHigh.Num() == Env.Num();

	// Each stretch of the wave wears the style scheduled there, blending across the boundary, so upcoming switches show as colour.
	// The blend from one style's colour to the next spans exactly the switch window (a bar ahead to just after),
	// and glows while it's open, so the player can see when a switch counts.
	const float Window = EclipseDance::SwitchWindowBeats * Track->BeatSeconds();
	const float Late = EclipseDance::JudgeOffset + EclipseDance::GoodLate;
	float Glow = 0.f;
	const auto StyleAt = [&](float T, bool bDeep)
	{
		const FLinearColor Intro = bDeep ? FLinearColor(0.2f, 0.2f, 0.26f) : FLinearColor(0.85f, 0.85f, 1.f);   // intro: pale
		FLinearColor Before = Intro, After = Intro;
		float At = -1.f;
		for (const TPair<float, EEclipseDanceStyle>& S : Switches)
		{
			if (S.Key - Window > T) break;
			Before = After;
			After = bDeep ? EclipseDance::StyleDeep(S.Value) : EclipseDance::StyleColor(S.Value);
			At = S.Key;
		}
		if (At < 0.f) { Glow = 0.f; return After; }
		const float A = FMath::Clamp((T - (At - Window)) / (Window + Late), 0.f, 1.f);
		Glow = (A > 0.f && A < 1.f) ? FMath::Sin(PI * A) : 0.f;
		return FMath::Lerp(Before, After, FMath::SmoothStep(0.f, 1.f, A));
	};

	// Three bands like a deck's waveform: a wide body, a brighter mid and a white-hot core that only the loud parts reach.
	constexpr float ColumnPx = 2.f;
	TArray<FVector2D> Top, Bottom;
	TArray<FLinearColor> EdgeColors;
	for (float X = 0.f; X < Size.X; X += ColumnPx)
	{
		const float T = Start + X / PxPerSec;
		const float E = Sample(Env, T);
		if (E < 0.f) continue;
		// Bass, mids and highs (or the plain loudness for tracks without bands): bass weighs down, highs spike up.
		const float Lo = bBands ? FMath::Max(0.f, Sample(Track->EnvelopeLow, T)) : E;
		const float Mi = bBands ? FMath::Max(0.f, Sample(Track->EnvelopeMid, T)) : FMath::Pow(E, 1.6f);
		const float Hi = bBands ? FMath::Max(0.f, Sample(Track->EnvelopeHigh, T)) : FMath::Pow(E, 3.f);
		// Warp: the centre line drifts on a slow wave so the shape bends instead of sitting dead flat.
		const float Y = MidY + FMath::Sin(X * 0.006f + Playhead * 1.3f) * MidY * 0.12f;
		const float Swell = 0.8f * (1.f + 0.35f * Pump) * FMath::Lerp(0.05f, 1.f, Presence) * (1.f + 0.35f * Glow) * (bHot ? 1.2f : 1.f);
		const FLinearColor Deep = StyleAt(T, true);
		FLinearColor Style = StyleAt(T, false);
		// Open window: brighter, so the switch zone stands out from the steady stretches.
		Style = (Style * (1.f + 0.8f * Glow) * (bHot ? 1.35f : 1.f)).GetClamped(0.f, 1.f);
		const FLinearColor Hot = FMath::Lerp(Style, FLinearColor::White, 0.5f);
		const float Past = (X < Size.X * 0.5f ? 0.5f : 1.f) * FMath::Lerp(0.2f, 1.f, Presence) * 0.6f;   // played audio dims; faint overall, fainter while the record spins up
		const float BassH = FMath::Min(Lo * MidY * Swell, MidY);
		const float MidH = FMath::Min(Mi * MidY * Swell * 0.8f, MidY);
		const float HighH = FMath::Min(Hi * MidY * Swell * 0.95f, MidY);
		DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - BassH * 0.45f), FVector2D(X, Y + BassH), FMath::Lerp(Deep, Style, Lo).CopyWithNewOpacity(0.6f * Past * (0.4f + 0.6f * Lo)), ColumnPx);
		if (MidH > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - MidH * 0.8f), FVector2D(X, Y + MidH * 0.5f), Style.CopyWithNewOpacity(0.55f * Past), ColumnPx);
		if (HighH > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - HighH), FVector2D(X, Y - HighH * 0.1f), FMath::Lerp(Hot, FLinearColor::White, Hi).CopyWithNewOpacity(0.7f * Past), ColumnPx);
		Top.Emplace(X, Y - FMath::Max3(BassH * 0.45f, MidH * 0.8f, HighH));
		Bottom.Emplace(X, Y + FMath::Max(BassH, MidH * 0.5f));
		EdgeColors.Add(Hot.CopyWithNewOpacity(0.5f));
	}
	// A crisp contour over the body, coloured along its length, so the shape reads at a glance.
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Top, EdgeColors, ESlateDrawEffect::None, FLinearColor::White, true, 1.5f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(), Bottom, EdgeColors, ESlateDrawEffect::None, FLinearColor::White, true, 1.5f);

	// Style switches glow as they come in; once past the playhead they melt back into the wave.
	for (const TPair<float, EEclipseDanceStyle>& S : Switches)
	{
		// Centred on the judged ideal, not the raw downbeat, so crossing the middle of the oval is a PERFECT.
		const float X = (S.Key + EclipseDance::JudgeOffset - Start) * PxPerSec;
		if (X < -200.f || X > Size.X + 200.f) continue;
		const float Fade = X >= Size.X * 0.5f ? 1.f : FMath::Clamp(1.f - (Size.X * 0.5f - X) / (Size.X * 0.12f), 0.f, 1.f);
		if (Fade <= 0.f) continue;
		// Starts as a short thin tick at the far edge and grows taller and wider as it reaches the playhead.
		const float Near = X >= Size.X * 0.5f ? 1.f - (X - Size.X * 0.5f) / (Size.X * 0.5f) : 1.f;
		const float Grow = FMath::Lerp(0.2f, 1.f, FMath::Clamp(Near, 0.f, 1.f));
		const float HalfH = Size.Y * 0.5f * Grow;
		// A long glowing oval marking the PERFECT moment; its white core is the PERFECT window once it reaches the playhead.
		const FLinearColor C = EclipseDance::StyleColor(S.Value);
		const float HalfWidths[] = { EclipseDance::PerfectHalf * 2.5f * PxPerSec, EclipseDance::PerfectHalf * 1.6f * PxPerSec, EclipseDance::PerfectHalf * PxPerSec };
		const FLinearColor Colors[] = { C.CopyWithNewOpacity(0.12f), C.CopyWithNewOpacity(0.35f), FMath::Lerp(C, FLinearColor::White, 0.6f) };
		for (int32 i = 0; i < 3; ++i)
		{
			const float Rx = HalfWidths[i] * Grow;
			for (float Dx = -Rx; Dx <= Rx; Dx += 3.f)
			{
				const float H = HalfH * FMath::Sqrt(FMath::Max(0.f, 1.f - FMath::Square(Dx / Rx)));
				DrawLine(OutDrawElements, LayerId + 3, AllottedGeometry, FVector2D(X + Dx, MidY - H), FVector2D(X + Dx, MidY + H), Colors[i] * FLinearColor(1.f, 1.f, 1.f, Fade * 0.7f), 3.f);
			}
		}
	}
	DrawLine(OutDrawElements, LayerId + 4, AllottedGeometry, FVector2D(Size.X * 0.5f, 0.f), FVector2D(Size.X * 0.5f, Size.Y), FLinearColor(1.f, 1.f, 1.f, 0.7f), 3.f);
	return LayerId + 4;
}

// ── Style wheel ──

// ── Battle screen ──

bool UEclipseDanceBattleWidget::Initialize()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("GradeText"))))
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
	auto Image = [&](FName Name, const FSlateBrush& Brush)
	{
		UImage* I = Tree->ConstructWidget<UImage>(UImage::StaticClass(), Name);
		I->SetBrush(Brush);
		I->SetVisibility(ESlateVisibility::HitTestInvisible);
		Fullscreen(I);
		return I;
	};

	// Back to front: a wash in the current style's colour, dark edges, then the beat-flashed edge glow.
	Image(TEXT("StyleTint"), SolidBrush(FLinearColor::White))->SetColorAndOpacity(FLinearColor::Transparent);
	FSlateBrush Dark = InnerGlowBrush(/*EdgePx=*/420.f);
	Dark.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.7f));
	Image(TEXT("Vignette"), Dark);
	FSlateBrush Glow = InnerGlowBrush(/*EdgePx=*/220.f);
	Glow.TintColor = FSlateColor(FLinearColor::White);   // colour comes from Pulse()
	Image(TEXT("StylePulse"), Glow)->SetRenderOpacity(0.f);

	auto Text = [&](FName Name, int32 Size, float Letter, const FString& Str = FString()) -> UTextBlock*
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo F = MakeBMSPA(Size, Letter);
		F.OutlineSettings.OutlineSize = 2;
		F.OutlineSettings.OutlineColor = FLinearColor::Black;
		T->SetFont(F);
		T->SetJustification(ETextJustify::Center);
		T->SetText(FText::FromString(Str));
		T->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		return T;
	};
	auto AddCentred = [](UVerticalBox* Box, UWidget* W)
	{
		if (UVerticalBoxSlot* S = Box->AddChildToVerticalBox(W)) S->SetHorizontalAlignment(HAlign_Center);
	};

	const auto Place = [Root](UWidget* W, float AnchorY, float OffsetY)
	{
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(W))
		{
			S->SetAnchors(FAnchors(0.5f, AnchorY, 0.5f, AnchorY));
			S->SetAlignment(FVector2D(0.5f, AnchorY));
			S->SetPosition(FVector2D(0.f, OffsetY));
			S->SetAutoSize(true);
		}
	};
	// Grades land at the bottom of the screen, just above the balance bar.
	Place(Text(TEXT("GradeText"), 34, 4.f), 1.f, -100.f);
	// A landed move's name flashes just above the grade.
	Place(Text(TEXT("ChainName"), 26, 6.f), 1.f, -180.f);
	// Who you're facing and their expert style, until the real battle starts.
	UTextBlock* Expert = Text(TEXT("ExpertText"), 22, 4.f);
	Expert->SetVisibility(ESlateVisibility::Hidden);
	Place(Expert, 0.f, 46.f);

	USizeBox* GaugeSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FacingGaugeSize"));
	GaugeSize->SetWidthOverride(440.f);
	GaugeSize->SetHeightOverride(30.f);
	GaugeSize->SetContent(Tree->ConstructWidget<UEclipseFacingGaugeWidget>(UEclipseFacingGaugeWidget::StaticClass(), TEXT("FacingGauge")));
	Place(GaugeSize, 1.f, -40.f);
	UTextBlock* BalanceLabel = Text(TEXT("BalanceLabel"), 14, 4.f, TEXT("BALANCE"));
	BalanceLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.45f)));
	Place(BalanceLabel, 1.f, -14.f);

	// His BALANCE bar, parked above his head every frame.
	UVerticalBox* StanceBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OpponentStanceBox"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(StanceBox))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		S->SetAlignment(FVector2D(0.5f, 1.f));   // sits on top of his head, moved there every frame
		S->SetSize(FVector2D(150.f, 12.f));
	}
	StanceBox->SetVisibility(ESlateVisibility::Collapsed);

	UProgressBar* StanceBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("OpponentStance"));
	StanceBar->SetFillColorAndOpacity(Cream);
	StanceBar->SetBarFillType(EProgressBarFillType::RightToLeft);
	StanceBar->SetPercent(1.f);
	if (UVerticalBoxSlot* S = StanceBox->AddChildToVerticalBox(StanceBar)) S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	// Attacks are two keys, not a wheel: the legend sits bottom-right with whatever style you're holding.
	UVerticalBox* Hints = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AttackHintBox"));
	UTextBlock* Light = Text(TEXT("AttackHintLight"), 22, 2.f, TEXT("UP - LIGHT"));
	UTextBlock* Heavy = Text(TEXT("AttackHintHeavy"), 22, 2.f, TEXT("DOWN - HEAVY"));
	Light->SetJustification(ETextJustify::Right);
	Heavy->SetJustification(ETextJustify::Right);
	UTextBlock* StyleName = Text(TEXT("StyleNameText"), 18, 3.f, TEXT("H  STYLE"));
	StyleName->SetJustification(ETextJustify::Right);
	for (UTextBlock* T : { Light, Heavy, StyleName })
	{
		if (UVerticalBoxSlot* S = Hints->AddChildToVerticalBox(T))
		{
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetPadding(FMargin(0.f, T == StyleName ? 10.f : 2.f, 0.f, 0.f));
		}
	}
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Hints))
	{
		S->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
		S->SetAlignment(FVector2D(1.f, 1.f));
		S->SetPosition(FVector2D(-40.f, -120.f));
		S->SetAutoSize(true);
	}

	}

void UEclipseDanceBattleWidget::ShowStyle(EEclipseDanceStyle Style)
{
	TintTarget = Style == EEclipseDanceStyle::Count ? FLinearColor::Transparent : EclipseDance::StyleColor(Style).CopyWithNewOpacity(0.12f);
}

void UEclipseDanceBattleWidget::SetFacingVisible(bool bVisible)
{
	if (FacingGauge) FacingGauge->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

void UEclipseDanceBattleWidget::ShatterStance()
{
	ShatterT = 0.f;
}

void UEclipseDanceBattleWidget::SetStanceScreenPos(const FVector2D& Pos, bool bVisible)
{
	if (!OpponentStance || !OpponentStance->GetParent()) return;
	UWidget* Box = OpponentStance->GetParent();
	Box->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bVisible)
	{
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Box->Slot)) S->SetPosition(Pos);
	}
}

void UEclipseDanceBattleWidget::SetStance(float Ratio, bool bHit)
{
	if (!OpponentStance) return;
	OpponentStance->SetPercent(Ratio);
	// Low stance reads as danger: the bar reddens as it empties.
	OpponentStance->SetFillColorAndOpacity(FMath::Lerp(EclipseUI::DialogueRed, EclipseUI::Cream, Ratio));
	if (bHit) StanceShake = 1.f;
}

int32 UEclipseFacingGaugeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	// A circle at the centre you sit inside; the marker sticks there while you're balanced, and the range
	// only appears once you've lost it.
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	constexpr float Span = 50.f;   // matches StrafeLimitDeg
	const float Mid = Size.X * 0.5f, Y = Size.Y * 0.5f, Half = Size.X * 0.5f - 6.f;
	const auto X = [&](float Deg) { return Mid + FMath::Clamp(Deg / Span, -1.f, 1.f) * Half; };
	const bool bFacing = FMath::Abs(ErrorDeg) <= WindowDeg;
	const float Danger = FMath::Clamp((FMath::Abs(ErrorDeg) - WindowDeg) / (Span - WindowDeg), 0.f, 1.f);
	// Stance, Sekiro fashion: it grows out of the centre both ways and turns white when it breaks.
	if (Pressure > 0.f)
	{
		const FLinearColor P = bStaggered ? FLinearColor::White : FMath::Lerp(EclipseDance::Gold, EclipseUI::DialogueRed, Pressure);
		const float W = Half * FMath::Min(1.f, Pressure);
		DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(Mid - W, Y), FVector2D(Mid + W, Y), P.CopyWithNewOpacity(bStaggered ? 0.95f : 0.5f), 22.f);
	}
	const FLinearColor Fill = bFacing ? FLinearColor(0.5f, 1.f, 0.6f) : FMath::Lerp(EclipseDance::Gold, EclipseUI::DialogueRed, Danger);
	DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(Mid - Half - 3.f, Y), FVector2D(Mid + Half + 3.f, Y), FLinearColor(0.f, 0.f, 0.f, 0.6f), 16.f);
	DrawLine(OutDrawElements, LayerId + 3, AllottedGeometry, FVector2D(Mid, Y), FVector2D(X(ErrorDeg), Y), Fill.CopyWithNewOpacity(0.35f), 18.f);   // glow
	DrawLine(OutDrawElements, LayerId + 4, AllottedGeometry, FVector2D(Mid, Y), FVector2D(X(ErrorDeg), Y), Fill, 8.f);
	// The ring is you: it drifts left and right with your position. Put it over the dot and you're matched.
	constexpr int32 Segments = 24;
	constexpr float Radius = 13.f;
	const float RingX = bFacing ? Mid + (X(ErrorDeg) - Mid) * 0.3f : X(ErrorDeg);   // sticky once you're matched
	TArray<FVector2D> Ring;
	for (int32 i = 0; i <= Segments; ++i)
	{
		const float A2 = 2.f * PI * i / Segments;
		Ring.Add(FVector2D(RingX + FMath::Cos(A2) * Radius, Y + FMath::Sin(A2) * Radius));
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 6, AllottedGeometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None,
		bFacing ? FLinearColor(0.5f, 1.f, 0.6f, 0.95f) : FLinearColor(1.f, 1.f, 1.f, 0.5f), true, 3.f);
	// The dot is him: dead centre, lit when you have him.
	const FLinearColor DotCol = bFacing ? FLinearColor(0.5f, 1.f, 0.6f) : FLinearColor(1.f, 1.f, 1.f, 0.7f);
	DrawLine(OutDrawElements, LayerId + 7, AllottedGeometry, FVector2D(Mid, Y - 1.f), FVector2D(Mid, Y + 1.f), DotCol, 7.f);
	return LayerId + 5;
}

void UEclipseDanceBattleWidget::ShowGrade(const FText& Grade, const FLinearColor& Color, float BeatSeconds)
{
	if (!GradeText) return;
	GradeText->SetText(Grade);
	GradeText->SetColorAndOpacity(FSlateColor(Color));
	if (BeatSeconds > 0.f) GradeBeat = BeatSeconds;
	GradeFade = 1.f;
}

void UEclipseDanceBattleWidget::FlashChain(const FString& Name, const FLinearColor& Color)
{
	if (!ChainName) return;
	ChainName->SetText(FText::FromString(Name));
	ChainName->SetColorAndOpacity(FSlateColor(Color));
	ChainFade = 1.f;
}

void UEclipseDanceBattleWidget::ShowExpert(const FString& Who, EEclipseDanceStyle Style)
{
	if (!ExpertText) return;
	const bool bShow = !Who.IsEmpty();
	ExpertText->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	if (!bShow) return;
	ExpertText->SetText(FText::FromString(FString::Printf(TEXT("%s - EXPERT: %s  %s"), *Who.ToUpper(),
		EclipseDance::Info(Style).Name, EclipseDance::IsHeavy(Style) ? TEXT("HEAVY / LEGS") : TEXT("LIGHT / HANDS"))));
	ExpertText->SetColorAndOpacity(FSlateColor(EclipseDance::StyleColor(Style)));
}

void UEclipseDanceBattleWidget::SetRadialSelected(EEclipseDanceStyle Style)
{
	if (!StyleNameText) return;
	const bool bPicked = Style != EEclipseDanceStyle::Count;
	StyleNameText->SetText(FText::FromString(bPicked ? FString::Printf(TEXT("H  %s"), EclipseDance::Info(Style).Name) : TEXT("H  STYLE")));
	StyleNameText->SetColorAndOpacity(FSlateColor(bPicked ? EclipseDance::StyleColor(Style) : EclipseUI::Cream));
}

void UEclipseDanceBattleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	}

void UEclipseDanceBattleWidget::Pulse(const FLinearColor& Color, float BeatSeconds, float Strength)
{
	PulseColor = Color;
	PulseA = 1.f;
	PulseStrength = Strength;
	PulseBeatSeconds = FMath::Max(0.1f, BeatSeconds);
}

void UEclipseDanceBattleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (StyleTint)
	{
		TintColor = FMath::CInterpTo(TintColor, TintTarget, InDeltaTime, 3.f);
		StyleTint->SetColorAndOpacity(TintColor);
	}
	if (OpponentStance)
	{
		StanceShake = FMath::Max(0.f, StanceShake - InDeltaTime * 4.f);
		OpponentStance->SetRenderTranslation(FVector2D(FMath::FRandRange(-6.f, 6.f) * StanceShake, 0.f));
	}
	if (StylePulse)
	{
		// Whichever is brighter wins: a pick's flash, or the backbeat swell.
		PulseA = FMath::Max(0.f, PulseA - InDeltaTime / PulseBeatSeconds);
		const float Pick = 0.55f * PulseStrength * PulseA * PulseA;
		const float Back = 0.5f * BackbeatLevel;
		StylePulse->SetColorAndOpacity(Pick > Back ? PulseColor : BackbeatColor);
		StylePulse->SetRenderOpacity(FMath::Max(Pick, Back));
	}
	if (GradeText)
	{
		GradeFade = FMath::Max(0.f, GradeFade - InDeltaTime * 0.8f);
		// Flashes in twice on landing, then breathes on the beat like his barks.
		const float Age = 1.f - GradeFade;
		const float Blink = Age < 0.24f ? (FMath::Fmod(Age, 0.12f) < 0.06f ? 0.f : 1.f) : 1.f;   // flashes in twice
		const float Pump = FMath::Square(1.f - FMath::Frac(Age / FMath::Max(0.1f, GradeBeat)));
		GradeText->SetRenderOpacity(FMath::Min(1.f, GradeFade * 2.f) * Blink * (0.55f + 0.45f * Pump));
		GradeText->SetRenderScale(FVector2D(1.f + 0.3f * Pump));
	}
	if (ShatterT >= 0.f && OpponentStance)
	{
		// Flares white, tips over and falls away.
		ShatterT += InDeltaTime;
		const float T = FMath::Clamp(ShatterT / 1.4f, 0.f, 1.f);
		OpponentStance->SetFillColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 1.f - T));
		OpponentStance->SetRenderTransformAngle(18.f * T);
		OpponentStance->SetRenderScale(FVector2D(1.f + 0.3f * FMath::Sin(PI * T), FMath::Max(0.05f, 1.f - T)));
		OpponentStance->SetRenderOpacity(1.f - T);
		if (UWidget* Box = OpponentStance->GetParent()) Box->SetRenderTranslation(FVector2D(0.f, 40.f * T * T));
	}
	if (ChainName)
	{
		// A landed chain punches out, then fades.
		ChainFade = FMath::Max(0.f, ChainFade - InDeltaTime * 1.2f);
		ChainName->SetRenderOpacity(FMath::Min(1.f, ChainFade * 2.f));
		ChainName->SetRenderScale(FVector2D(1.f + 0.35f * ChainFade * ChainFade));
	}
}
