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
	const auto Sample = [&](float T)
	{
		// Interpolated between samples so it isn't stair-stepped, with a touch of smoothing.
		const float F = T * Rate;
		const int32 I = FMath::FloorToInt(F);
		if (!Env.IsValidIndex(I) || !Env.IsValidIndex(I + 1)) return -1.f;
		const float Here = FMath::Lerp(Env[I], Env[I + 1], F - I);
		const float Around = 0.5f * (Env[FMath::Max(0, I - 1)] + Env[FMath::Min(Env.Num() - 1, I + 2)]);
		return FMath::Lerp(Here, Around, 0.25f);
	};

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
		const float E = Sample(Start + X / PxPerSec);
		if (E < 0.f) continue;
		// Warp: the centre line drifts on a slow wave so the shape bends instead of sitting dead flat.
		const float Y = MidY + FMath::Sin(X * 0.006f + Playhead * 1.3f) * MidY * 0.12f;
		const float Swell = 0.8f * (1.f + 0.35f * Pump) * FMath::Lerp(0.05f, 1.f, Presence);
		const FLinearColor Deep = StyleAt(Start + X / PxPerSec, true);
		FLinearColor Style = StyleAt(Start + X / PxPerSec, false);
		// Open window: brighter, and the body swells, so the switch zone stands out from the steady stretches.
		const float Heat = bHot ? 1.35f : 1.f;
		Style = (Style * (1.f + 0.8f * Glow) * Heat).GetClamped(0.f, 1.f);
		const FLinearColor Hot = FMath::Lerp(Style, FLinearColor::White, 0.5f);
		const float Past = (X < Size.X * 0.5f ? 0.5f : 1.f) * FMath::Lerp(0.2f, 1.f, Presence);   // played audio dims; everything's faint while the record spins up
		const float Body = FMath::Clamp(E * MidY * Swell * (1.f + 0.35f * Glow) * (bHot ? 1.2f : 1.f), 1.5f, MidY);
		const float MidH = FMath::Pow(E, 1.6f) * MidY * Swell * 0.72f;
		const float Core = FMath::Pow(E, 3.f) * MidY * Swell * 0.45f;
		FLinearColor C = FMath::Lerp(Deep, Style, E);   // the style's own gradient: quiet runs deep, loud runs bright
		DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - Body), FVector2D(X, Y + Body), C.CopyWithNewOpacity(0.55f * Past * (0.4f + 0.6f * E)), ColumnPx);
		if (MidH > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - MidH), FVector2D(X, Y + MidH), Hot.CopyWithNewOpacity(0.6f * Past), ColumnPx);
		if (Core > 1.f) DrawLine(OutDrawElements, LayerId + 1, AllottedGeometry, FVector2D(X, Y - Core), FVector2D(X, Y + Core), FLinearColor(1.f, 1.f, 1.f, 0.75f * Past), ColumnPx);
		Top.Emplace(X, Y - Body);
		Bottom.Emplace(X, Y + Body);
		EdgeColors.Add(Hot.CopyWithNewOpacity(0.8f));
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
				DrawLine(OutDrawElements, LayerId + 3, AllottedGeometry, FVector2D(X + Dx, MidY - H), FVector2D(X + Dx, MidY + H), Colors[i] * FLinearColor(1.f, 1.f, 1.f, Fade), 3.f);
			}
		}
	}
	DrawLine(OutDrawElements, LayerId + 4, AllottedGeometry, FVector2D(Size.X * 0.5f, 0.f), FVector2D(Size.X * 0.5f, Size.Y), FLinearColor(1.f, 1.f, 1.f, 0.7f), 3.f);
	return LayerId + 4;
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
	const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();

	const auto Polar = [&](float Deg, float Radius) { return C + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg)), -FMath::Sin(FMath::DegreesToRadians(Deg))) * Radius; };
	const auto Arc = [&](float FromDeg, float ToDeg, float Radius, float Thick, const FLinearColor& Col, int32 Layer)
	{
		TArray<FVector2D> Pts;
		for (int32 i = 0; i <= 16; ++i) Pts.Add(Polar(FMath::Lerp(FromDeg, ToDeg, i / 16.f), Radius));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, Paint, Pts, ESlateDrawEffect::None, Col, true, Thick);
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
		if (!((UnlockedMask >> i) & 1))
		{
			// Undiscovered: plain grey with no gaps, so neighbouring locked slots read as one unbroken arc.
			Arc(Info.WheelDeg - SlotSpan * 0.5f, Info.WheelDeg + SlotSpan * 0.5f, Mid, Outer - Inner, FLinearColor(0.3f, 0.3f, 0.33f, 0.45f), LayerId + 2);
			continue;
		}
		FLinearColor Col = EclipseDance::StyleColor(S);
		Col.A = bSel ? 1.f : 0.55f;
		const float Lift = bSel ? 6.f + 6.f * SelectPunch : 0.f;
		Arc(Info.WheelDeg - SlotSpan * 0.5f + Gap, Info.WheelDeg + SlotSpan * 0.5f - Gap, Mid + Lift, Outer - Inner + (bSel ? 8.f : 0.f), Col, LayerId + 2);
		DrawCentredText(OutDrawElements, LayerId + 3, AllottedGeometry, Info.Combo, ComboFont, Polar(Info.WheelDeg, Mid + Lift), bSel ? FLinearColor::Black : FLinearColor(1.f, 1.f, 1.f, 0.7f));
		FLinearColor NameCol = EclipseDance::StyleColor(S);
		NameCol.A = bSel ? 1.f : 0.55f;
		DrawCentredText(OutDrawElements, LayerId + 3, AllottedGeometry, Info.Name, NameFont, Polar(Info.WheelDeg, Outer + 22.f), NameCol);
	}
	if (Selected != EEclipseDanceStyle::Count)
	{
		DrawCentredText(OutDrawElements, LayerId + 3, AllottedGeometry, EclipseDance::Info(Selected).Name, EclipseUI::MakeBMSPA(15, 2.f), C, EclipseDance::StyleColor(Selected));
	}
	return LayerId + 3;
}

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
	// The countdown is just the number, in the incoming style's colour, a little way down from the top.
	UTextBlock* Count = Text(TEXT("CountNumber"), 56, 0.f);
	Count->SetVisibility(ESlateVisibility::Hidden);
	Place(Count, 0.f, 110.f);
	// Grades land at the bottom of the screen.
	Place(Text(TEXT("GradeText"), 34, 4.f), 1.f, -70.f);

	USizeBox* GaugeSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("FacingGaugeSize"));
	GaugeSize->SetWidthOverride(360.f);
	GaugeSize->SetHeightOverride(70.f);
	GaugeSize->SetContent(Tree->ConstructWidget<UEclipseFacingGaugeWidget>(UEclipseFacingGaugeWidget::StaticClass(), TEXT("FacingGauge")));
	Place(GaugeSize, 1.f, -120.f);

	UVerticalBox* HeatBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BattleHeatBox"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(HeatBox))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		S->SetPosition(FVector2D(40.f, 40.f));
		S->SetSize(FVector2D(320.f, 60.f));
	}
	UTextBlock* HeatLabel = Text(TEXT("BattleHeatLabel"), 20, 4.f, TEXT("HEAT"));
	HeatLabel->SetJustification(ETextJustify::Left);
	HeatBox->AddChildToVerticalBox(HeatLabel);
	UProgressBar* HeatBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("BattleHeat"));
	HeatBar->SetFillColorAndOpacity(HeatRed);
	HeatBar->SetRenderTransformPivot(FVector2D(0.f, 0.5f));
	if (UVerticalBoxSlot* S = HeatBox->AddChildToVerticalBox(HeatBar)) S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

	// The opponent's STANCE, top-right opposite your HEAT: For Honor's guard, knocked down by landed switches.
	UVerticalBox* StanceBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("OpponentStanceBox"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(StanceBox))
	{
		S->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		S->SetAlignment(FVector2D(1.f, 0.f));
		S->SetPosition(FVector2D(-40.f, 40.f));
		S->SetSize(FVector2D(320.f, 60.f));
	}
	UTextBlock* StanceLabel = Text(TEXT("OpponentStanceLabel"), 20, 4.f, TEXT("STANCE"));
	StanceLabel->SetJustification(ETextJustify::Right);
	StanceBox->AddChildToVerticalBox(StanceLabel);
	UProgressBar* StanceBar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("OpponentStance"));
	StanceBar->SetFillColorAndOpacity(Cream);
	StanceBar->SetBarFillType(EProgressBarFillType::RightToLeft);
	StanceBar->SetPercent(1.f);
	if (UVerticalBoxSlot* S = StanceBox->AddChildToVerticalBox(StanceBar)) S->SetPadding(FMargin(0.f, 4.f, 0.f, 0.f));

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
	if (UVerticalBoxSlot* S = ResultsCol->AddChildToVerticalBox(Verdict))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
	}
	// Same transparent-until-hovered button as the pause menu.
	UButton* Continue = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ContinueBtn"));
	FButtonStyle BS;
	BS.Normal   = SolidBrush(FLinearColor::Transparent);
	BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.08f));
	BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
	BS.Disabled = SolidBrush(FLinearColor::Transparent);
	Continue->SetStyle(BS);
	UTextBlock* ContinueLabel = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ContinueBtn_Label"));
	ContinueLabel->SetText(FText::FromString(TEXT("CONTINUE")));
	ContinueLabel->SetFont(MakeRodin(22));
	ContinueLabel->SetColorAndOpacity(FSlateColor(Cream));
	Continue->SetContent(ContinueLabel);
	Continue->SetVisibility(ESlateVisibility::Hidden);
	if (UVerticalBoxSlot* S = ResultsCol->AddChildToVerticalBox(Continue))
	{
		S->SetHorizontalAlignment(HAlign_Center);
		S->SetPadding(FMargin(0.f, 20.f, 0.f, 0.f));
	}
}

void UEclipseDanceBattleWidget::ShowStyle(EEclipseDanceStyle Style)
{
	TintTarget = Style == EEclipseDanceStyle::Count ? FLinearColor::Transparent : EclipseDance::StyleColor(Style).CopyWithNewOpacity(0.12f);
}

void UEclipseDanceBattleWidget::SetStance(float Ratio, bool bHit)
{
	if (!OpponentStance) return;
	OpponentStance->SetPercent(Ratio);
	// Low stance reads as danger: the bar reddens as it empties.
	OpponentStance->SetFillColorAndOpacity(FMath::Lerp(EclipseUI::DialogueRed, EclipseUI::Cream, Ratio));
	if (bHit) StanceShake = 1.f;
}

void UEclipseDanceBattleWidget::SetHeat(int32 Heat, bool bInHeatMode)
{
	bHeatMode = bInHeatMode;
	if (BattleHeat) BattleHeat->SetPercent(Heat / 10.f);
}

int32 UEclipseFacingGaugeWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	// A shallow arc spanning +-60 degrees: the lit notch in the middle is "facing him"; the marker is where you are.
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D C(Size.X * 0.5f, Size.Y * 2.2f);
	const float R = Size.Y * 1.9f;
	constexpr float Span = 60.f;
	const auto At = [&](float Deg) { const float A = FMath::DegreesToRadians(-90.f + Deg * 0.6f); return C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * R; };
	const auto Arc = [&](float From, float To, const FLinearColor& Col, float Thick, int32 Layer)
	{
		TArray<FVector2D> Pts;
		for (int32 i = 0; i <= 24; ++i) Pts.Add(At(FMath::Lerp(From, To, i / 24.f)));
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Pts, ESlateDrawEffect::None, Col, true, Thick);
	};
	const bool bFacing = FMath::Abs(ErrorDeg) <= WindowDeg;
	Arc(-Span, Span, FLinearColor(1.f, 1.f, 1.f, 0.18f), 6.f, LayerId + 1);
	Arc(-WindowDeg, WindowDeg, bFacing ? FLinearColor(0.5f, 1.f, 0.6f, 0.9f) : FLinearColor(1.f, 1.f, 1.f, 0.35f), 10.f, LayerId + 2);
	// Drifting off turns the marker red and fattens it, so it's obvious at a glance.
	const FVector2D M = At(FMath::Clamp(ErrorDeg, -Span, Span));
	const FLinearColor MarkCol = bFacing ? FLinearColor::White : EclipseUI::DialogueRed;
	DrawLine(OutDrawElements, LayerId + 3, AllottedGeometry, M - FVector2D(0.f, 14.f), M + FVector2D(0.f, 14.f), MarkCol, bFacing ? 4.f : 7.f);
	return LayerId + 3;
}

void UEclipseDanceBattleWidget::ShowCountdown(int32 Count, EEclipseDanceStyle Next, float BeatSeconds)
{
	if (!CountNumber) return;
	if (Count <= 0)
	{
		CountNumber->SetVisibility(ESlateVisibility::Hidden);
		return;
	}
	CountNumber->SetColorAndOpacity(FSlateColor(EclipseDance::StyleColor(Next)));
	CountNumber->SetText(FText::AsNumber(Count));
	CountNumber->SetVisibility(ESlateVisibility::HitTestInvisible);
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

void UEclipseDanceBattleWidget::SetUnlockedStyles(int32 Mask)
{
	if (StyleWheel) StyleWheel->SetUnlocked(Mask);
}

void UEclipseDanceBattleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ContinueBtn) ContinueBtn->OnClicked.AddUniqueDynamic(this, &UEclipseDanceBattleWidget::HandleContinueClicked);
}

void UEclipseDanceBattleWidget::Pulse(const FLinearColor& Color, float BeatSeconds, float Strength)
{
	PulseColor = Color;
	PulseA = 1.f;
	PulseStrength = Strength;
	PulseBeatSeconds = FMath::Max(0.1f, BeatSeconds);
}

void UEclipseDanceBattleWidget::ShowResults(const TArray<FEclipseDanceResultRow>& Rows, const FString& Verdict, const FLinearColor& VerdictColor, bool bInVerdictGlow)
{
	if (!ResultsPanel || !ResultsBox) return;
	TallyTick = LoadObject<USoundBase>(nullptr, TEXT("/Game/Justin/Audio/UI/S_UI_DialogueLine.S_UI_DialogueLine"));

	ResultsBox->ClearChildren();
	ResultRows.Reset();
	float At = 0.f;
	for (const FEclipseDanceResultRow& R : Rows)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const FSlateFontInfo Font = EclipseUI::MakeBMSPA(22, 2.f);
		UTextBlock* Name = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Name->SetFont(Font);
		Name->SetText(FText::FromString(R.Label));
		Name->SetColorAndOpacity(FSlateColor(R.Color));
		UTextBlock* Value = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Value->SetFont(Font);
		Value->SetJustification(ETextJustify::Right);
		Value->SetColorAndOpacity(FSlateColor(R.Color));
		Value->SetShadowOffset(FVector2D::ZeroVector);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Name)) S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Row->AddChildToHorizontalBox(Value);
		Row->SetVisibility(ESlateVisibility::Hidden);   // keeps its space so the board doesn't grow as it fills
		if (UVerticalBoxSlot* S = ResultsBox->AddChildToVerticalBox(Row)) S->SetPadding(FMargin(0.f, 3.f));
		// Bigger numbers take longer to roll up, like an arcade tally.
		const float Count = R.Text.IsEmpty() ? 0.3f + 0.18f * FString::FromInt(FMath::Max(1, R.Value)).Len() : 0.25f;
		if (!R.Text.IsEmpty()) Value->SetText(FText::FromString(R.Text));
		ResultRows.Add({ Value, R.Text.IsEmpty() ? R.Value : -1, -1, At, Count, R.bGlow });
		At += Count + 0.2f;
	}
	VerdictAt = At + 0.1f;
	bVerdictGlow = bInVerdictGlow;
	if (VerdictText)
	{
		VerdictText->SetText(FText::FromString(Verdict));
		VerdictText->SetColorAndOpacity(FSlateColor(VerdictColor));
		VerdictText->SetShadowOffset(FVector2D::ZeroVector);
		VerdictText->SetVisibility(ESlateVisibility::Hidden);
	}
	ResultsPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);   // CONTINUE inside it takes clicks
	ResultsClock = 0.f;
	LastTick = -1.f;
}

void UEclipseDanceBattleWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (StyleTint)
	{
		TintColor = FMath::CInterpTo(TintColor, TintTarget, InDeltaTime, 3.f);
		StyleTint->SetColorAndOpacity(TintColor);
	}
	if (BattleHeat)
	{
		// HEAT mode: the bar burns white-hot and throbs, like Yakuza's.
		HeatGlowT += InDeltaTime;
		const float Throb = bHeatMode ? 0.5f + 0.5f * FMath::Sin(HeatGlowT * 9.f) : 0.f;
		BattleHeat->SetFillColorAndOpacity(FMath::Lerp(EclipseUI::HeatRed, FLinearColor(1.f, 0.85f, 0.5f), Throb));
		BattleHeat->SetRenderScale(FVector2D(1.f, 1.f + 0.6f * Throb));
		if (BattleHeatLabel)
		{
			BattleHeatLabel->SetText(FText::FromString(bHeatMode ? TEXT("HEAT MODE") : TEXT("HEAT")));
			BattleHeatLabel->SetColorAndOpacity(FSlateColor(bHeatMode ? FLinearColor(1.f, 0.7f, 0.3f) : EclipseUI::Cream));
		}
	}
	if (OpponentStance)
	{
		StanceShake = FMath::Max(0.f, StanceShake - InDeltaTime * 4.f);
		OpponentStance->SetRenderTranslation(FVector2D(FMath::FRandRange(-6.f, 6.f) * StanceShake, 0.f));
	}
	// HEAT mode brightens the whole screen tint.
	if (StyleTint && bHeatMode) StyleTint->SetColorAndOpacity(TintColor * FLinearColor(1.6f, 1.4f, 1.2f, 2.f));
	if (StylePulse)
	{
		// Whichever is brighter wins: a pick's flash, or the backbeat swell.
		PulseA = FMath::Max(0.f, PulseA - InDeltaTime / PulseBeatSeconds);
		const float Pick = 0.55f * PulseStrength * PulseA * PulseA;
		const float Back = 0.5f * BackbeatLevel;
		StylePulse->SetColorAndOpacity(Pick > Back ? PulseColor : BackbeatColor);
		StylePulse->SetRenderOpacity(FMath::Max(Pick, Back));
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
	if (ResultsClock >= 0.f)
	{
		ResultsClock += InDeltaTime;
		const float Glow = 0.55f + 0.45f * FMath::Sin(ResultsClock * 5.f);   // platinum shimmer
		for (FResultRow& R : ResultRows)
		{
			const float T = ResultsClock - R.Start;
			if (T < 0.f || !R.Value) continue;
			if (R.Shown < 0) R.Value->GetParent()->SetVisibility(ESlateVisibility::HitTestInvisible);
			if (R.Target < 0) { R.Shown = 0; if (R.bGlow) R.Value->SetShadowColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f * Glow)); continue; }   // a word, not a number
			const float Progress = FMath::Clamp(T / R.Count, 0.f, 1.f);
			const int32 Now = FMath::RoundToInt(R.Target * Progress);
			if (Now != R.Shown)
			{
				R.Shown = Now;
				R.Value->SetText(FText::AsNumber(Now));
				// Ticks rise in pitch as the number climbs; throttled so a big score rattles rather than buzzes.
				if (TallyTick && ResultsClock - LastTick > 0.04f)
				{
					LastTick = ResultsClock;
					if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
					{
						Audio->PlayUI(TallyTick, 0.35f, 0.9f + 0.7f * Progress);
					}
				}
			}
			if (R.bGlow) R.Value->SetShadowColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f * Glow));
		}
		if (VerdictText && ResultsClock >= VerdictAt && !VerdictText->IsVisible())
		{
			VerdictText->SetVisibility(ESlateVisibility::HitTestInvisible);
			VerdictPunch = 1.f;
			if (ContinueBtn) ContinueBtn->SetVisibility(ESlateVisibility::Visible);
		}
		VerdictPunch = FMath::Max(0.f, VerdictPunch - InDeltaTime * 3.f);
		if (VerdictText)
		{
			VerdictText->SetRenderScale(FVector2D(1.f + 0.5f * VerdictPunch * VerdictPunch));
			if (bVerdictGlow) VerdictText->SetShadowColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.8f * Glow));
		}
	}
}
