// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePauseMenuWidget.h"
#include "eclipse.h"
#include "EclipseUiStyle.h"
#include "EclipseBlinkWipeWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "EclipseHUD.h"
#include "EclipseHUDWidget.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Pause menu — CONTINUE / QUIT, then "Last saved: X ago". No manual saves.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Show/hide the gameplay HUD (meter bars + clock) while the pause menu
	// is up. The HUD is added to the viewport at ZOrder 200 and the pause
	// menu at 100, so without this the bars and clock draw straight over
	// the menu — raising the menu's ZOrder instead would put it above the
	// blink-wipe too, so it's cleaner to just hide the HUD.
	void SetGameplayHUDVisible(APlayerController* PC, bool bVisible)
	{
		if (!PC) return;
		AEclipseHUD* HUD = Cast<AEclipseHUD>(PC->GetHUD());
		if (!HUD || !HUD->HUDWidget) return;
		HUD->HUDWidget->SetVisibility(bVisible
			? ESlateVisibility::SelfHitTestInvisible
			: ESlateVisibility::Collapsed);
	}
}

UEclipsePauseMenuWidget* UEclipsePauseMenuWidget::OpenForPlayer(APlayerController* PC)
{
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu::OpenForPlayer entered (PC=%s)"),
		PC ? *PC->GetName() : TEXT("<null>"));
	if (!PC) return nullptr;

	// Prefer the designer-styled WBP_PauseMenu — same pattern as Dialogue/HUD.
	// Falls back to the pure C++ class (which builds its own tree at runtime
	// via BuildFallbackTree) if the WBP isn't authored yet.
	TSubclassOf<UEclipsePauseMenuWidget> WidgetClass = UEclipsePauseMenuWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipsePauseMenuWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_PauseMenu.WBP_PauseMenu_C")))
	{
		WidgetClass = BPClass;
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu::OpenForPlayer — using WBP_PauseMenu_C"));
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu::OpenForPlayer — WBP not found, using C++ fallback"));
	}

	UEclipsePauseMenuWidget* W = CreateWidget<UEclipsePauseMenuWidget>(
		PC, WidgetClass, TEXT("PauseMenu"));
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu::OpenForPlayer — CreateWidget=%s"),
		W ? TEXT("OK") : TEXT("FAILED"));
	if (!W) return nullptr;

	W->AddToViewport(/*ZOrder=*/900);   // over the dance battle screen and yaps; under the black-out screen
	W->SetIsFocusable(true);
	W->SetKeyboardFocus();
	SetGameplayHUDVisible(PC, false);
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu::OpenForPlayer — added to viewport"));

	// Dump the live widget tree so we can see whether the fallback build
	// produced a populated hierarchy or just an empty root.
	if (W->WidgetTree)
	{
		TArray<UWidget*> All;
		W->WidgetTree->GetAllWidgets(All);
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu: tree has %d widgets, root=%s, visibility=%d, desired=%s"),
			All.Num(),
			W->WidgetTree->RootWidget ? *W->WidgetTree->RootWidget->GetName() : TEXT("none"),
			(int32)W->GetVisibility(),
			*W->GetDesiredSize().ToString());
		for (UWidget* Wd : All)
		{
			if (Wd)
			{
				UE_LOG(LogEclipse, Log, TEXT("  - %s (%s)"),
					*Wd->GetName(), *Wd->GetClass()->GetName());
			}
		}
	}

	// Pause world + UI input mode + cursor on. SetGamePaused respects a
	// PlayerController, so input that's bound to "executes when paused"
	// (which we set on the IA later if needed) still fires.
	// Music drags down like a deck losing power, then the world pauses; input goes to the menu straight away.
	TWeakObjectPtr<UWorld> PauseWorld = W->GetWorld();
	if (UEclipseAudioSubsystem* Audio = PC->GetGameInstance() ? PC->GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->RampDeckSpeed(0.4f, 0.9f, [PauseWorld]() { if (PauseWorld.IsValid()) UGameplayStatics::SetGamePaused(PauseWorld.Get(), true); });
	}
	else
	{
		UGameplayStatics::SetGamePaused(W->GetWorld(), true);
	}
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);

	return W;
}

void UEclipsePauseMenuWidget::Close()
{
	APlayerController* PC = GetOwningPlayer();
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGamePaused(W, false);
	}
	// And spins back up to speed.
	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->RampDeckSpeed(1.f, 0.35f);
	}
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
	SetGameplayHUDVisible(PC, true);
	RemoveFromParent();
}

bool UEclipsePauseMenuWidget::Initialize()
{
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu::Initialize — WidgetTree=%s root=%s"),
		WidgetTree ? TEXT("set") : TEXT("null"),
		(WidgetTree && WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("none"));

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("ResumeBtn"))))
	{
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu::Initialize — building fallback tree"));
		BuildFallbackTree();
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu::Initialize — root after build: %s"),
			(WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("STILL NONE"));
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu::Initialize — fallback skipped (ResumeBtn already in tree, or tree null)"));
	}
	return Super::Initialize();
}

void UEclipsePauseMenuWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Full-screen dim so the world behind reads as paused.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor::Black));   // fully opaque — no world showing through
	Dim->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		S->SetZOrder(0);
	}

	// Full-bleed chalk panel — Border content stretches the full viewport.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PausePanel"));
	Panel->SetBrush(SolidBrush(FLinearColor::Black));
	// Left-aligned column with a margin off the screen edge.
	Panel->SetPadding(FMargin(80.f, 0.f, 0.f, 0.f));
	Panel->SetHorizontalAlignment(HAlign_Left);
	Panel->SetVerticalAlignment(VAlign_Center);

	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Panel->SetContent(Column);

	// No heading — the menu is just the list of options.


	// Helper that builds a button + label, parented to a given vertical box.
	auto MakeBtnIn = [&](UVerticalBox* Parent, const FString& Label, FName WidgetName,
		int32 FontSize = 22, TObjectPtr<UTextBlock>* OutLabel = nullptr) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
		FButtonStyle BS;
		// Transparent in every state — plain text, not buttons. The
		// UButton survives only to carry click + hover.
		BS.Normal   = SolidBrush(FLinearColor::Transparent);
		BS.Hovered  = SolidBrush(FLinearColor::Transparent);
		BS.Pressed  = SolidBrush(FLinearColor::Transparent);
		BS.Disabled = SolidBrush(FLinearColor::Transparent);
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(FText::FromString(Label));
		T->SetFont(MakeRodin(FontSize));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Left);
		Btn->SetContent(T);
		if (OutLabel) *OutLabel = T;
		if (UButtonSlot* BSlot = Cast<UButtonSlot>(T->Slot))
		{
			BSlot->SetHorizontalAlignment(HAlign_Left);
			BSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* VS = Parent->AddChildToVerticalBox(Btn))
		{
			VS->SetPadding(FMargin(0.f, 6.f));
			VS->SetHorizontalAlignment(HAlign_Left);
		}
		return Btn;
	};

	UVerticalBox* MainList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainList"));
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(MainList))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	// QUIT returns to the main menu (MainMenuBtn's handler).
	ResumeBtn   = MakeBtnIn(MainList, TEXT("CONTINUE"), TEXT("ResumeBtn"));
	MainMenuBtn = MakeBtnIn(MainList, TEXT("QUIT"),     TEXT("MainMenuBtn"));

	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::GetEmpty());
	{
		FSlateFontInfo F = MakeRodin(14);
		F.SkewAmount = 0.2f;   // the font ships no italic face, so fake the slant
		StatusText->SetFont(F);
	}
	StatusText->SetColorAndOpacity(FSlateColor(EclipseUI::CreamDim));
	StatusText->SetJustification(ETextJustify::Left);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusText))
	{
		VS->SetPadding(FMargin(0.f, 48.f, 0.f, 0.f));
		VS->SetHorizontalAlignment(HAlign_Left);
	}
}

void UEclipsePauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ResumeBtn)   ResumeBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnResume);
	if (MainMenuBtn) MainMenuBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnMainMenu);
	if (QuitBtn)     QuitBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnQuit);

	// Force MouseDown click-method on each — single-click select like dialogue.
	UButton* AllBtns[] = { ResumeBtn, MainMenuBtn, QuitBtn };
	for (UButton* B : AllBtns) if (B) B->SetClickMethod(EButtonClickMethod::MouseDown);
}

void UEclipsePauseMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!StatusText) return;
	if (const UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		StatusText->SetText(GS->GetLastSavedText());
	}
}

FReply UEclipsePauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	if (K == EKeys::Escape) { OnResume(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UEclipsePauseMenuWidget::OnResume()
{
	// Resume button — same blink-wipe-around-close pattern as PC::TogglePauseMenu.
	// The wipe goes to fully-black, Close() runs (game unpause + input swap +
	// RemoveFromParent), then the wipe fades back. Self is GC-safe inside
	// the lambda because PlayFull's tick fires synchronously next frame
	// while the widget is still alive.
	APlayerController* PC = GetOwningPlayer();
	if (!PC) { Close(); return; }
	TWeakObjectPtr<UEclipsePauseMenuWidget> WeakSelf(this);
	UEclipseBlinkWipeWidget::FOnBlinkPhase Cb;
	Cb.BindLambda([WeakSelf]()
	{
		if (WeakSelf.IsValid()) WeakSelf->Close();
	});
	UEclipseBlinkWipeWidget::PlayFull(PC, Cb);
}

void UEclipsePauseMenuWidget::OnMainMenu()
{
	// Unpause first so OpenLevel doesn't hit the paused-world fast-path,
	// then reset the LocalPlayer's input mode so the OUTGOING UIOnly mode
	// from this menu doesn't leak into the next level's PC.
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);   // L_MainMenu wants cursor; OK to keep on
	}
	// Leaving is the last chance to save; without it CONTINUE could rewind the session.
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->Autosave();
	}

	UWorld* W = GetWorld();
	if (!W) return;
	UGameplayStatics::SetGamePaused(W, false);

	// Eye-shut wipe — OpenLevel happens at fully-covered. Destination
	// level should run UEclipseBlinkWipeWidget::PlayOpen on BeginPlay.
	const FName Target = MainMenuLevelName;
	UEclipseBlinkWipeWidget::FOnBlinkPhase OnClosed;
	OnClosed.BindLambda([W, Target]()
	{
		UGameplayStatics::OpenLevel(W, Target);
	});
	UEclipseBlinkWipeWidget::PlayClose(PC, OnClosed);
}

void UEclipsePauseMenuWidget::OnQuit()
{
	APlayerController* PC = GetOwningPlayer();
	UKismetSystemLibrary::QuitGame(GetWorld(), PC,
		EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}
