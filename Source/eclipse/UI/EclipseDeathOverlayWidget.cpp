// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDeathOverlayWidget.h"
#include "eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UEclipseDeathOverlayWidget* UEclipseDeathOverlayWidget::OpenForPlayer(APlayerController* PC)
{
	if (!PC) return nullptr;

	TSubclassOf<UEclipseDeathOverlayWidget> Cls = UEclipseDeathOverlayWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseDeathOverlayWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_DeathOverlay.WBP_DeathOverlay_C")))
	{
		Cls = BPClass;
	}

	UEclipseDeathOverlayWidget* W = CreateWidget<UEclipseDeathOverlayWidget>(
		PC, Cls, TEXT("DeathOverlay"));
	if (!W) return nullptr;

	W->AddToViewport(/*ZOrder=*/1000);   // above everything else
	W->SetIsFocusable(true);

	UGameplayStatics::SetGamePaused(W->GetWorld(), true);

	// GameAndUI (not UIOnly) so the PC's pause-menu Esc binding still works
	// as an escape hatch — though the modal also offers TRY AGAIN / QUIT,
	// matching the dialogue / stats / pause menu pattern.
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	W->SetKeyboardFocus();

	UE_LOG(LogEclipse, Log, TEXT("DeathOverlay: opened for %s"), *PC->GetName());
	return W;
}

void UEclipseDeathOverlayWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (FadeT >= 1.f) return;

	// The world sinks into black over ~1.5 s, then the panel surfaces out of it.
	FadeT = FMath::Min(1.f, FadeT + InDeltaTime / 1.5f);
	for (const TCHAR* Name : { TEXT("Dim"), TEXT("PassOutVignette"), TEXT("PassOutVignetteDeep") })
	{
		if (UWidget* W = WidgetTree->FindWidget(FName(Name))) W->SetRenderOpacity(FadeT);
	}
	if (UWidget* Panel = WidgetTree->FindWidget(FName(TEXT("DeathPanel"))))
	{
		Panel->SetRenderOpacity(FMath::Clamp((FadeT - 0.5f) * 2.f, 0.f, 1.f));
	}
}

void UEclipseDeathOverlayWidget::Close()
{
	if (bDismissed) return;
	bDismissed = true;

	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGamePaused(W, false);
	}
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
	RemoveFromParent();
}

bool UEclipseDeathOverlayWidget::Initialize()
{
	// Pure-C++ user-widget path: WidgetTree starts null because there's no
	// BP archetype. Allocate one so BuildFallbackTree has somewhere to land.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transactional);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("TryAgainBtn"))))
	{
		BuildFallbackTree();
	}
	return Super::Initialize();
}

void UEclipseDeathOverlayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (TryAgainBtn)
	{
		TryAgainBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		TryAgainBtn->OnClicked.AddDynamic(this, &UEclipseDeathOverlayWidget::OnTryAgainClicked);
	}
	if (QuitBtn)
	{
		QuitBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		QuitBtn->OnClicked.AddDynamic(this, &UEclipseDeathOverlayWidget::OnQuitClicked);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Button handlers
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDeathOverlayWidget::OnTryAgainClicked()
{
	UE_LOG(LogEclipse, Log, TEXT("DeathOverlay: TRY AGAIN"));

	// Back to the last autosave (map reloaded, player at the saved spot); with no save, reset the meters in place.
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	Close();
	if (!GS || GS->RetryFromSave()) return;
	UE_LOG(LogEclipse, Log, TEXT("DeathOverlay: no save, hard-resetting meters"));
	// Heat must come back above 0 or the player would respawn already passed out.
	GS->Heat   = 8;
	GS->Thirst = 5;
	// Rebase the bleed markers, or the next AdvanceGameTime re-applies every interval spent passed out.
	GS->LastHeatDecayAtSeconds   = GS->ChapterElapsedSeconds;
	GS->LastThirstDecayAtSeconds = GS->ChapterElapsedSeconds;
}

void UEclipseDeathOverlayWidget::OnQuitClicked()
{
	UE_LOG(LogEclipse, Log, TEXT("DeathOverlay: QUIT -> main menu"));
	UWorld* World = GetWorld();
	Close();
	if (World)
	{
		// Match the pause-menu QUIT path: just bounce to the main-menu level.
		UGameplayStatics::OpenLevel(World, FName(TEXT("L_MainMenu")));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout — fallback tree
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDeathOverlayWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// The passing-out filter, back to front over the frozen frame: blur it,
	// darken it, then close the edges in. Dim stays light enough that the
	// blurred night is still visible behind it — that's the effect; a solid
	// black would just hide it.
	auto Fullscreen = [Root](UWidget* W)
	{
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(W))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}
	};

	UBackgroundBlur* Blur = WidgetTree->ConstructWidget<UBackgroundBlur>(
		UBackgroundBlur::StaticClass(), TEXT("PassOutBlur"));
	Blur->SetBlurStrength(12.f);
	Fullscreen(Blur);

	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.8f)));
	Dim->SetRenderOpacity(0.f);   // faded in by NativeTick
	Fullscreen(Dim);

	// Vignette: the same edge-gradient texture as the HUD's inner glow,
	// tinted black and stretched hundreds of pixels in from every side.
	UImage* Vignette = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PassOutVignette"));
	{
		FSlateBrush V = InnerGlowBrush(/*EdgePx=*/560.f);
		V.TintColor = FSlateColor(FLinearColor::Black);
		Vignette->SetBrush(V);
	}
	Vignette->SetVisibility(ESlateVisibility::HitTestInvisible);
	Vignette->SetRenderOpacity(0.f);
	Fullscreen(Vignette);
	// Stacked twice so the edges go all the way to black.
	UImage* Vignette2 = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PassOutVignetteDeep"));
	Vignette2->SetBrush(Vignette->GetBrush());
	Vignette2->SetVisibility(ESlateVisibility::HitTestInvisible);
	Vignette2->SetRenderOpacity(0.f);
	Fullscreen(Vignette2);

	// Centred panel — same RoundedBrush(PanelBg, PanelBorder) navy as HUD
	// and stats menu so the visual language is consistent.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeathPanel"));
	Panel->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
	Panel->SetPadding(FMargin(36.f, 28.f));
	Panel->SetRenderOpacity(0.f);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		// Sized to content: "YOU PASSED OUT" is twice the width of the old
		// title and would overflow a fixed box.
		S->SetAutoSize(true);
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DeathColumn"));
	Panel->SetContent(Column);

	// Title — red-shifted cyan to match the bleeding-energy pulse.
	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DeathTitle"));
	Title->SetText(FText::FromString(TEXT("YOU PASSED OUT")));
	Title->SetFont(MakeBerenjena(72, 10.f));
	Title->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.30f, 0.25f, 1.f)));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 40.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	// Helper — one button. Mirrors the pause-menu MakeBtn pattern.
	auto MakeBtn = [&](const FString& Label, FName WidgetName) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
		FButtonStyle BS;
		BS.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.05f));
		BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
		BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.22f));
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		Btn->SetStyle(BS);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(FText::FromString(Label));
		T->SetFont(MakeBerenjena(28, 5.f));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);

		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Btn))
		{
			VS->SetPadding(FMargin(0.f, 8.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		return Btn;
	};

	TryAgainBtn = MakeBtn(TEXT("RETRY"), TEXT("TryAgainBtn"));
	QuitBtn     = MakeBtn(TEXT("QUIT"),  TEXT("QuitBtn"));
}
