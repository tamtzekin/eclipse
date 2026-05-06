// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseChapterCardWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Subsystems/EclipseGameStateSubsystem.h"

bool UEclipseChapterCardWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("BlackBg"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// Fullscreen black background
		BlackBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BlackBg"));
		BlackBg->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));   // alpha 0 — invisible at rest
		BlackBg->SetPadding(FMargin(0.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(BlackBg))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		}

		// Centred text column inside the black bg
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("ChapterColumn"));
		BlackBg->SetContent(Column);
		BlackBg->SetHorizontalAlignment(HAlign_Center);
		BlackBg->SetVerticalAlignment(VAlign_Center);

		// Optional subtitle — small BMSPA cyan label, e.g. "CHAPTER 1"
		SubtitleText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("SubtitleText"));
		SubtitleText->SetFont(MakeBMSPA(/*Size=*/16, /*Letter=*/4.f));
		SubtitleText->SetColorAndOpacity(FSlateColor(Cyan));
		SubtitleText->SetJustification(ETextJustify::Center);
		SubtitleText->SetText(FText::FromString(TEXT("CHAPTER 1")));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(SubtitleText))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}

		// Big title — RodinPro cream
		TitleText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("TitleText"));
		TitleText->SetFont(MakeRodin(/*Size=*/48));
		TitleText->SetColorAndOpacity(FSlateColor(Cream));
		TitleText->SetJustification(ETextJustify::Center);
		TitleText->SetText(FText::FromString(TEXT("Night Begins")));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(TitleText))
		{
			VS->SetHorizontalAlignment(HAlign_Center);
		}
	}

	return Super::Initialize();
}

void UEclipseChapterCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);   // always present, alpha-driven
	SetAlpha(0.f);

	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnChapterCardRequested.AddDynamic(this, &UEclipseChapterCardWidget::HandleChapterCardRequested);
	}
}

void UEclipseChapterCardWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnChapterCardRequested.RemoveDynamic(this, &UEclipseChapterCardWidget::HandleChapterCardRequested);
		}
	}
	Super::NativeDestruct();
}

void UEclipseChapterCardWidget::HandleChapterCardRequested(const FText& Title)
{
	Show(Title);
}

void UEclipseChapterCardWidget::Show(const FText& Title)
{
	if (TitleText) TitleText->SetText(Title);
	Phase = EPhase::FadeIn;
	PhaseSeconds = 0.f;
	UE_LOG(LogEclipse, Log, TEXT("ChapterCard: Show '%s'"), *Title.ToString());
}

void UEclipseChapterCardWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);
	if (Phase == EPhase::Idle) return;

	PhaseSeconds += DeltaSeconds;

	switch (Phase)
	{
		case EPhase::FadeIn:
		{
			const float A = FMath::Clamp(PhaseSeconds / FMath::Max(0.01f, FadeInSeconds), 0.f, 1.f);
			SetAlpha(A);
			if (PhaseSeconds >= FadeInSeconds)
			{
				Phase = EPhase::Hold;
				PhaseSeconds = 0.f;
			}
			break;
		}
		case EPhase::Hold:
		{
			SetAlpha(1.f);
			if (PhaseSeconds >= HoldSeconds)
			{
				Phase = EPhase::FadeOut;
				PhaseSeconds = 0.f;
			}
			break;
		}
		case EPhase::FadeOut:
		{
			const float A = 1.f - FMath::Clamp(PhaseSeconds / FMath::Max(0.01f, FadeOutSeconds), 0.f, 1.f);
			SetAlpha(A);
			if (PhaseSeconds >= FadeOutSeconds)
			{
				Phase = EPhase::Idle;
				PhaseSeconds = 0.f;
				SetAlpha(0.f);
			}
			break;
		}
		default: break;
	}
}

void UEclipseChapterCardWidget::SetAlpha(float A)
{
	A = FMath::Clamp(A, 0.f, 1.f);

	if (BlackBg)
	{
		// Fade the panel itself via brush tint alpha
		BlackBg->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, A));
	}
	if (TitleText)
	{
		FLinearColor C = EclipseUI::Cream; C.A = A;
		TitleText->SetColorAndOpacity(FSlateColor(C));
	}
	if (SubtitleText)
	{
		FLinearColor C = EclipseUI::Cyan; C.A = A;
		SubtitleText->SetColorAndOpacity(FSlateColor(C));
	}
}
