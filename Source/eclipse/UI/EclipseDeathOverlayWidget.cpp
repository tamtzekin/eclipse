// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDeathOverlayWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
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

	// Restore the most recent slot. We don't track "last used" yet — try
	// slot 0 first. If empty, the LoadFromSlot call returns false and we
	// fall through to a full restart (reset meters to defaults so the
	// player isn't stuck dead immediately).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			const bool bLoaded = GS->LoadFromSlot(0);
			UE_LOG(LogEclipse, Log, TEXT("DeathOverlay: LoadFromSlot[0] -> %s"),
				bLoaded ? TEXT("OK") : TEXT("no save, hard-resetting meters"));
			if (!bLoaded)
			{
				// Mirror the subsystem defaults — sweet-spot middle for
				// Thirst, slightly low for Heat. Heat must come back above
				// 0 or the player would respawn already dead.
				GS->Heat   = 3;
				GS->Thirst = 5;
			}
		}
	}
	Close();
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

	// Heavy dim — death screens deserve more darkness than menus.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.85f)));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	// Centred panel — same RoundedBrush(PanelBg, PanelBorder) navy as HUD
	// and stats menu so the visual language is consistent.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DeathPanel"));
	Panel->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
	Panel->SetPadding(FMargin(36.f, 28.f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(520.f, 320.f));
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DeathColumn"));
	Panel->SetContent(Column);

	// Title — red-shifted cyan to match the bleeding-energy pulse.
	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DeathTitle"));
	Title->SetText(FText::FromString(TEXT("YOU DIED")));
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

	TryAgainBtn = MakeBtn(TEXT("TRY AGAIN"), TEXT("TryAgainBtn"));
	QuitBtn     = MakeBtn(TEXT("QUIT"),      TEXT("QuitBtn"));
}
