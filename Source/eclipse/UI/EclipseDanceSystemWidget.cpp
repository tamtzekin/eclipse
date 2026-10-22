// Copyright (c) ECLIPSE. All Rights Reserved.

#include "UI/EclipseDanceSystemWidget.h"
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
		DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - BassH * 0.45f), FVector2D(X, Y + BassH), FMath::Lerp(Deep, Style, Lo).CopyWithNewOpacity(0.35f * Past * (0.4f + 0.6f * Lo)), ColumnPx);
		if (MidH > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - MidH * 0.8f), FVector2D(X, Y + MidH * 0.5f), Style.CopyWithNewOpacity(0.32f * Past), ColumnPx);
		if (HighH > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - HighH), FVector2D(X, Y - HighH * 0.1f), FMath::Lerp(Hot, FLinearColor::White, Hi).CopyWithNewOpacity(0.4f * Past), ColumnPx);
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

bool UEclipseDanceSystemWidget::Initialize()
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

void UEclipseDanceSystemWidget::BuildTree(UWidgetTree* Tree)
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
	GaugeSize->SetWidthOverride(260.f);
	GaugeSize->SetHeightOverride(40.f);   // oval needs the room
	GaugeSize->SetContent(Tree->ConstructWidget<UEclipseFacingGaugeWidget>(UEclipseFacingGaugeWidget::StaticClass(), TEXT("FacingGauge")));
	Place(GaugeSize, 1.f, -40.f);
	UTextBlock* BalanceLabel = Text(TEXT("BalanceLabel"), 14, 4.f, TEXT("STANCE"));
	BalanceLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.45f)));
	Place(BalanceLabel, 1.f, -14.f);

	// The lesson list, drawn like the game's objective list: diamond bullet, struck through once passed.
	UVerticalBox* Steps = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TutorialSteps"));
	const TCHAR* StepNames[] = { TEXT("TutorialStep1"), TEXT("TutorialStep2"), TEXT("TutorialStep3") };
	const TCHAR* DotNames[] = { TEXT("TutorialDot1"), TEXT("TutorialDot2"), TEXT("TutorialDot3") };
	const TCHAR* StrikeNames[] = { TEXT("TutorialStrike1"), TEXT("TutorialStrike2"), TEXT("TutorialStrike3") };
	for (int32 i = 0; i < 3; ++i)
	{
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), NAME_None);
		const float Side = 11.f * 0.72f;
		USizeBox* DotBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), NAME_None);
		DotBox->SetWidthOverride(Side);
		DotBox->SetHeightOverride(Side);
		UBorder* Dot = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), DotNames[i]);
		Dot->SetBrush(DiamondBrush(FLinearColor::Transparent, Cream, 1.5f));
		Dot->SetRenderTransformAngle(45.f);
		Dot->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		DotBox->AddChild(Dot);
		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(DotBox))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			HS->SetPadding(FMargin(2.f, BulletTopPadding(MakeFragmentMono(14), Side), 8.f, 0.f));
			HS->SetVerticalAlignment(VAlign_Top);
		}
		UOverlay* Cell = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), NAME_None);
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), StepNames[i]);
		FSlateFontInfo F = MakeFragmentMono(14);
		F.OutlineSettings.OutlineSize = 2;
		F.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.9f);
		T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(Cream));
		if (UOverlaySlot* OS = Cell->AddChildToOverlay(T)) { OS->SetHorizontalAlignment(HAlign_Left); OS->SetVerticalAlignment(VAlign_Top); }
		UBorder* Strike = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), StrikeNames[i]);
		Strike->SetBrush(SolidBrush(Cream));
		Strike->SetVisibility(ESlateVisibility::Collapsed);
		Strike->SetPadding(FMargin(0.f, 1.f));
		if (UOverlaySlot* OS = Cell->AddChildToOverlay(Strike))
		{
			OS->SetHorizontalAlignment(HAlign_Fill);
			OS->SetVerticalAlignment(VAlign_Top);
			OS->SetPadding(FMargin(0.f, 7.f, 0.f, 0.f));
		}
		if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(Cell)) HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		if (UVerticalBoxSlot* VS = Steps->AddChildToVerticalBox(Row)) VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}
	Steps->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Steps))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		S->SetPosition(FVector2D(40.f, 40.f));
		S->SetAutoSize(true);
	}

	// His BALANCE bar, parked above his head every frame.
	UVerticalBox* StanceBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OpponentStanceBox"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(StanceBox))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		S->SetAlignment(FVector2D(0.5f, 1.f));   // sits on top of his head, moved there every frame
		S->SetSize(FVector2D(150.f, 12.f));
	}
	StanceBox->SetVisibility(ESlateVisibility::Collapsed);

	USizeBox* StanceSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("OpponentStanceSize"));
	StanceSize->SetWidthOverride(150.f);
	StanceSize->SetHeightOverride(14.f);
	StanceSize->SetContent(Tree->ConstructWidget<UEclipseFacingGaugeWidget>(UEclipseFacingGaugeWidget::StaticClass(), TEXT("OpponentStance")));
	if (UVerticalBoxSlot* S = StanceBox->AddChildToVerticalBox(StanceSize)) S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	// Attacks are two keys, not a wheel: the legend sits bottom-right with whatever style you're holding.
	UVerticalBox* Hints = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AttackHintBox"));
	UTextBlock* Light = Text(TEXT("AttackHintLight"), 22, 2.f, TEXT("\u25B2 LIGHT"));
	UTextBlock* Heavy = Text(TEXT("AttackHintHeavy"), 22, 2.f, TEXT("\u25BC HEAVY"));
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

void UEclipseDanceSystemWidget::ShowStyle(EEclipseDanceStyle Style)
{
	TintTarget = Style == EEclipseDanceStyle::Count ? FLinearColor::Transparent : EclipseDance::StyleColor(Style).CopyWithNewOpacity(0.12f);
}

void UEclipseDanceSystemWidget::SetFacingVisible(bool bVisible)
{
	if (FacingGauge) FacingGauge->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
}

void UEclipseDanceSystemWidget::FadeOutStance()
{
	StanceFadeT = 0.f;
}

void UEclipseDanceSystemWidget::SetTutorialStep(int32 Active)
{
	using namespace EclipseUI;
	UTextBlock* Steps[] = { TutorialStep1, TutorialStep2, TutorialStep3 };
	UBorder* Dots[] = { TutorialDot1, TutorialDot2, TutorialDot3 };
	UBorder* Strikes[] = { TutorialStrike1, TutorialStrike2, TutorialStrike3 };
	const TCHAR* Names[] = { TEXT("Keep your balance"), TEXT("Match him, move for move"), TEXT("Read him faster") };
	if (UWidget* Box = TutorialStep1 ? TutorialStep1->GetParent() : nullptr)
	{
		if (UWidget* List = Box->GetParent() ? Box->GetParent()->GetParent() : nullptr)
		{
			List->SetVisibility(Active < 0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}
	for (int32 i = 0; i < 3; ++i)
	{
		const bool bDone = Active > i, bNow = Active == i;
		if (Steps[i])
		{
			Steps[i]->SetText(FText::FromString(TutorialLabels.IsValidIndex(i) && !TutorialLabels[i].IsEmpty()
				? TutorialLabels[i] : FString(Names[i])));
			Steps[i]->SetColorAndOpacity(FSlateColor(bDone ? FLinearColor(1.f, 1.f, 1.f, 0.45f) : bNow ? Cream : FLinearColor(1.f, 1.f, 1.f, 0.5f)));
		}
		// Filled diamond and a line through it once it's passed, same as a finished objective.
		if (Dots[i]) Dots[i]->SetBrush(DiamondBrush(bDone ? Cream : FLinearColor::Transparent, Cream, 1.5f));
		if (Strikes[i]) Strikes[i]->SetVisibility(bDone ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UEclipseDanceSystemWidget::SetStanceScreenPos(const FVector2D& Pos, bool bVisible)
{
	if (!OpponentStance) return;
	// Walk up to whichever ancestor actually sits on the canvas — the bar is wrapped in a size box.
	UWidget* Box = OpponentStance;
	while (Box && !Cast<UCanvasPanelSlot>(Box->Slot)) Box = Box->GetParent();
	if (!Box) return;
	Box->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bVisible)
	{
		if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(Box->Slot)) S->SetPosition(Pos);
	}
}

void UEclipseDanceSystemWidget::SetStance(float Ratio, bool bHit)
{
	if (!OpponentStance) return;
	OpponentStance->SetBarOnly(true);
	OpponentStance->SetBarColors(EclipseDance::Gold, EclipseDance::BloodRed);
	OpponentStance->SetStance(1.f - Ratio, /*bStaggered=*/false);   // his bar fills as you break him, same as yours
	if (bHit)
	{
		StanceShake = 1.f;
		OpponentStance->FlashHit();
	}
}

int32 UEclipseFacingGaugeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	// A circle at the centre you sit inside; the marker sticks there while you're balanced, and the range
	// only appears once you've lost it.
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	constexpr float Span = 45.f;   // wider than the 30 deg window, so losing him is visible before it bites
	const float Mid = Size.X * 0.5f, Half = Size.X * 0.5f - 6.f;
	const float Y = bBarOnly ? Size.Y * 0.5f : Size.Y * 0.36f;

	// Posture, Sekiro style: empty while you're steady, filling out from the centre as it goes.
	// Breathes as it fills and flashes on every hit, so it's readable without looking straight at it.
	{
		const float BarY = bBarOnly ? Y : Size.Y - 6.f;
		const float Fill = FMath::Clamp(Pressure, 0.f, 1.f);
		DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(Mid - Half, BarY), FVector2D(Mid + Half, BarY), FLinearColor(0.f, 0.f, 0.f, 0.55f), 11.f);
		if (Fill > 0.002f)
		{
			const float Pulse = 0.82f + 0.18f * FMath::Sin(PulseTime * (4.f + 6.f * Fill));
			FLinearColor C = FMath::Lerp(BarColor * Pulse, FlashColor, HitFlash);
			C.A = 1.f;
			const float W = Half * Fill;
			DrawLine(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2D(Mid - W, BarY), FVector2D(Mid + W, BarY), C, 9.f + 3.f * HitFlash);
		}
	}
	if (bBarOnly) return LayerId + 3;
	const auto X = [&](float Deg) { return Mid + FMath::Clamp(Deg / Span, -1.f, 1.f) * Half; };
	const bool bFacing = FMath::Abs(ErrorDeg) <= WindowDeg;
	const float Danger = FMath::Clamp((FMath::Abs(ErrorDeg) - WindowDeg) / (Span - WindowDeg), 0.f, 1.f);
	// Yellow as soon as you leave the ring, red by the time you're out of his reach.
	const FLinearColor Fill = bFacing ? FLinearColor(0.5f, 1.f, 0.6f) : FMath::Lerp(FLinearColor(1.f, 0.85f, 0.2f), EclipseUI::DialogueRed, Danger);
	if (bStaggered) DrawLine(OutDrawElements, LayerId + 2, AllottedGeometry, FVector2D(Mid - Half, Y), FVector2D(Mid + Half, Y), FLinearColor(1.f, 1.f, 1.f, 0.8f), 14.f);
	DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(Mid - Half - 3.f, Y), FVector2D(Mid + Half + 3.f, Y), FLinearColor(0.f, 0.f, 0.f, 0.6f), 16.f);
	// He's the filled circle in the middle; you're the ring that drifts across it.
	// Sit yours inside his and you're facing him; stray outside and nothing you throw lands.
	constexpr int32 Segments = 36;
	// He's a dot at the centre; your oval is wide enough to sit over it while you're facing him.
	const float HimDot = 5.f, MeX = 48.f, MeY = 15.f;
	const auto Oval = [&](float Cx, float Rx, float Ry, const FLinearColor& Col, float Thick, int32 Layer)
	{
		TArray<FVector2D> Ring;
		for (int32 i = 0; i <= Segments; ++i)
		{
			const float A2 = 2.f * PI * i / Segments;
			Ring.Add(FVector2D(Cx + FMath::Cos(A2) * Rx, Y + FMath::Sin(A2) * Ry));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Ring, ESlateDrawEffect::None, Col, true, Thick);
	};
	// Filled by stacking rings; Slate has no disc primitive.
	for (float R = 1.5f; R <= HimDot; R += 1.5f) Oval(Mid, R, R, bFacing ? FLinearColor(0.5f, 1.f, 0.6f, 0.95f) : FLinearColor(0.85f, 0.85f, 0.9f, 0.85f), 3.f, LayerId + 6);
	Oval(X(ErrorDeg), MeX, MeY, Fill, 2.5f, LayerId + 7);
	return LayerId + 7;
}

void UEclipseFacingGaugeWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	PulseTime += InDeltaTime;
	HitFlash = FMath::Max(0.f, HitFlash - InDeltaTime * 3.f);
}

void UEclipseDanceSystemWidget::ShowGrade(const FText& Grade, const FLinearColor& Color)
{
	if (!GradeText) return;
	GradeText->SetText(Grade);
	GradeText->SetColorAndOpacity(FSlateColor(Color));
	GradeFade = 1.f;
}

void UEclipseDanceSystemWidget::SetStyleVisible(bool bVisible)
{
	if (StyleNameText) StyleNameText->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UEclipseDanceSystemWidget::FlashChain(const FString& Name, const FLinearColor& Color)
{
	if (!ChainName) return;
	ChainName->SetText(FText::FromString(Name));
	ChainName->SetColorAndOpacity(FSlateColor(Color));
	ChainFade = 1.f;
}

void UEclipseDanceSystemWidget::ShowExpert(const FString& Who, EEclipseDanceStyle Style)
{
	if (!ExpertText) return;
	const bool bShow = !Who.IsEmpty();
	bExpertFading = !bShow;   // clearing it starts a fade rather than cutting it
	if (!bShow) return;
	ExpertFade = 1.f;
	ExpertText->SetRenderOpacity(1.f);
	ExpertText->SetVisibility(ESlateVisibility::HitTestInvisible);
	ExpertText->SetText(FText::FromString(FString::Printf(TEXT("%s - EXPERT: %s  %s"), *Who.ToUpper(),
		EclipseDance::Info(Style).Name, EclipseDance::IsHeavy(Style) ? TEXT("HEAVY / LEGS") : TEXT("LIGHT / HANDS"))));
	ExpertText->SetColorAndOpacity(FSlateColor(EclipseDance::StyleColor(Style)));
}

void UEclipseDanceSystemWidget::SetRadialSelected(EEclipseDanceStyle Style)
{
	if (!StyleNameText) return;
	const bool bPicked = Style != EEclipseDanceStyle::Count;
	StyleNameText->SetText(FText::FromString(bPicked ? FString::Printf(TEXT("H  %s"), EclipseDance::Info(Style).Name) : TEXT("H  STYLE")));
	StyleNameText->SetColorAndOpacity(FSlateColor(bPicked ? EclipseDance::StyleColor(Style) : EclipseUI::Cream));
}

void UEclipseDanceSystemWidget::NativeConstruct()
{
	Super::NativeConstruct();
	}

void UEclipseDanceSystemWidget::Pulse(const FLinearColor& Color, float BeatSeconds, float Strength)
{
	PulseColor = Color;
	PulseA = 1.f;
	PulseStrength = Strength;
	PulseBeatSeconds = FMath::Max(0.1f, BeatSeconds);
}

void UEclipseDanceSystemWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
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
		const float Pump = BeatPulse;
		GradeText->SetRenderOpacity(FMath::Min(1.f, GradeFade * 2.f) * Blink * (0.55f + 0.45f * Pump));
		GradeText->SetRenderScale(FVector2D(1.f + 0.3f * Pump));
	}
	if (StanceFadeT >= 0.f && OpponentStance)
	{
		// Broken: it just goes out where it stands.
		StanceFadeT += InDeltaTime;
		OpponentStance->SetRenderOpacity(1.f - FMath::Clamp(StanceFadeT / 0.9f, 0.f, 1.f));
	}
	if (ExpertText && bExpertFading)
	{
		ExpertFade = FMath::Max(0.f, ExpertFade - InDeltaTime * 0.8f);
		ExpertText->SetRenderOpacity(ExpertFade);
		if (ExpertFade <= 0.f) { ExpertText->SetVisibility(ESlateVisibility::Hidden); bExpertFading = false; }
	}
	if (ChainName)
	{
		// A landed chain punches out, then fades.
		ChainFade = FMath::Max(0.f, ChainFade - InDeltaTime * 1.2f);
		ChainName->SetRenderOpacity(FMath::Min(1.f, ChainFade * 2.f));
		ChainName->SetRenderScale(FVector2D(1.f + 0.35f * ChainFade * ChainFade));
	}
}
