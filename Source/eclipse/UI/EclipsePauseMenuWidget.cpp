// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePauseMenuWidget.h"
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
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Pause menu — single-screen overlay. Centred panel, 360×320, chalk style.
//
//   [PAUSED]
//   ─────────────
//   Resume
//   Save
//   Load
//   Main Menu
//   Quit
//   <status line>
// ─────────────────────────────────────────────────────────────────────────────

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

	W->AddToViewport(/*ZOrder=*/100);
	W->SetIsFocusable(true);
	W->SetKeyboardFocus();
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
	UGameplayStatics::SetGamePaused(W->GetWorld(), true);
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
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
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
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.55f)));
	Dim->SetPadding(FMargin(0.f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		S->SetZOrder(0);
	}

	// Full-bleed chalk panel — Border content stretches the full viewport.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PausePanel"));
	Panel->SetBrush(SolidBrush(FLinearColor(0.039f, 0.043f, 0.059f, 0.96f)));
	Panel->SetPadding(FMargin(0.f));
	Panel->SetHorizontalAlignment(HAlign_Fill);
	Panel->SetVerticalAlignment(VAlign_Center);

	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Panel->SetContent(Column);

	// Title — gigantic
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PauseTitle"));
	Title->SetText(FText::FromString(TEXT("PAUSED")));
	Title->SetFont(MakeBMSPA(/*Size=*/160, /*Letter=*/14.f));
	Title->SetColorAndOpacity(FSlateColor(Cyan));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 80.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	// Helper to build a button row — full-width, big font.
	auto MakeBtn = [&](const FString& Label, FName WidgetName) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
		FButtonStyle BS;
		BS.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.05f));
		BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
		BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.22f));
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Label));
		T->SetFont(MakeRodin(56));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);

		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Btn))
		{
			VS->SetPadding(FMargin(0.f, 14.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		return Btn;
	};

	ResumeBtn   = MakeBtn(TEXT("RESUME"),     TEXT("ResumeBtn"));
	SaveBtn     = MakeBtn(TEXT("SAVE"),       TEXT("SaveBtn"));
	LoadBtn     = MakeBtn(TEXT("LOAD"),       TEXT("LoadBtn"));
	MainMenuBtn = MakeBtn(TEXT("MAIN MENU"),  TEXT("MainMenuBtn"));
	QuitBtn     = MakeBtn(TEXT("QUIT"),       TEXT("QuitBtn"));

	// Status line — reports save/load result.
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatusText"));
	StatusText->SetText(FText::GetEmpty());
	StatusText->SetFont(MakeRodin(28));
	StatusText->SetColorAndOpacity(FSlateColor(EclipseUI::CreamDim));
	StatusText->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusText))
	{
		VS->SetPadding(FMargin(0.f, 48.f, 0.f, 0.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}
}

void UEclipsePauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ResumeBtn)   ResumeBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnResume);
	if (SaveBtn)     SaveBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnSave);
	if (LoadBtn)     LoadBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnLoad);
	if (MainMenuBtn) MainMenuBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnMainMenu);
	if (QuitBtn)     QuitBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnQuit);
}

FReply UEclipsePauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	if (K == EKeys::Escape) { OnResume(); return FReply::Handled(); }
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UEclipsePauseMenuWidget::OnResume() { Close(); }

void UEclipsePauseMenuWidget::OnSave()
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	const bool bOk = GS && GS->SaveCurrent();
	SetStatus(bOk ? TEXT("Saved.") : TEXT("Save failed."));
}

void UEclipsePauseMenuWidget::OnLoad()
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	const bool bOk = GS && GS->TryLoadCurrent();
	SetStatus(bOk ? TEXT("Loaded.") : TEXT("No save found."));
}

void UEclipsePauseMenuWidget::OnMainMenu()
{
	if (UWorld* W = GetWorld())
	{
		// Unpause first so OpenLevel doesn't hit the paused-world fast-path.
		UGameplayStatics::SetGamePaused(W, false);
		UGameplayStatics::OpenLevel(W, MainMenuLevelName);
	}
}

void UEclipsePauseMenuWidget::OnQuit()
{
	APlayerController* PC = GetOwningPlayer();
	UKismetSystemLibrary::QuitGame(GetWorld(), PC,
		EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UEclipsePauseMenuWidget::SetStatus(const FString& Msg)
{
	if (StatusText) StatusText->SetText(FText::FromString(Msg));
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu: %s"), *Msg);
}
