// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseMainMenuWidget.h"
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
#include "Save/EclipseSaveGame.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/PlayerController.h"

UEclipseMainMenuWidget* UEclipseMainMenuWidget::OpenForPlayer(APlayerController* PC)
{
	if (!PC) return nullptr;

	// Prefer the WBP-backed class (designer-styled). Fall back to the C++
	// fallback tree if the WBP isn't present yet.
	TSubclassOf<UEclipseMainMenuWidget> Cls = UEclipseMainMenuWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseMainMenuWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_MainMenu.WBP_MainMenu_C")))
	{
		Cls = BPClass;
	}

	UEclipseMainMenuWidget* W = CreateWidget<UEclipseMainMenuWidget>(PC, Cls, TEXT("MainMenu"));
	if (!W) return nullptr;

	W->AddToViewport(/*ZOrder=*/100);
	W->SetIsFocusable(true);
	W->SetKeyboardFocus();

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	return W;
}

bool UEclipseMainMenuWidget::Initialize()
{
	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("NewGameBtn"))))
	{
		BuildFallbackTree();
	}
	return Super::Initialize();
}

void UEclipseMainMenuWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Full-screen chalk panel — same style as the pause menu so the boot
	// experience feels consistent with the in-game pause overlay.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MainMenuPanel"));
	Panel->SetBrush(SolidBrush(FLinearColor(0.039f, 0.043f, 0.059f, 1.f)));
	Panel->SetPadding(FMargin(0.f));
	Panel->SetHorizontalAlignment(HAlign_Fill);
	Panel->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("MainMenuColumn"));
	Panel->SetContent(Column);

	// Title
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MainMenuTitle"));
	Title->SetText(FText::FromString(TEXT("ECLIPSE")));
	Title->SetFont(MakeBMSPA(/*Size=*/200, /*Letter=*/18.f));
	Title->SetColorAndOpacity(FSlateColor(Cyan));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 96.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

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

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(FText::FromString(Label));
		T->SetFont(MakeRodin(56));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);

		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Btn))
		{
			VS->SetPadding(FMargin(0.f, 16.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		return Btn;
	};

	NewGameBtn  = MakeBtn(TEXT("NEW GAME"),  TEXT("NewGameBtn"));
	ContinueBtn = MakeBtn(TEXT("CONTINUE"),  TEXT("ContinueBtn"));
	QuitBtn     = MakeBtn(TEXT("QUIT"),      TEXT("QuitBtn"));

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

void UEclipseMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (NewGameBtn)  NewGameBtn->OnClicked.AddDynamic(this, &UEclipseMainMenuWidget::OnNewGame);
	if (ContinueBtn) ContinueBtn->OnClicked.AddDynamic(this, &UEclipseMainMenuWidget::OnContinue);
	if (QuitBtn)     QuitBtn->OnClicked.AddDynamic(this, &UEclipseMainMenuWidget::OnQuit);

	// Force MouseDown on each — single-click select, like the pause menu.
	UButton* AllBtns[] = { NewGameBtn, ContinueBtn, QuitBtn };
	for (UButton* B : AllBtns) if (B) B->SetClickMethod(EButtonClickMethod::MouseDown);

	RefreshContinueState();
}

void UEclipseMainMenuWidget::RefreshContinueState()
{
	// Disable the Continue button if there's no autosave on disk.
	const bool bHasAutosave = UGameplayStatics::DoesSaveGameExist(
		UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex);
	if (ContinueBtn) ContinueBtn->SetIsEnabled(bHasAutosave);
	if (StatusText && !bHasAutosave)
	{
		StatusText->SetText(FText::FromString(TEXT("No save game found.")));
	}
}

void UEclipseMainMenuWidget::OnNewGame()
{
	UE_LOG(LogEclipse, Log, TEXT("MainMenu: New Game — wiping autosave + opening %s"),
		*NewGameLevelName.ToString());

	// Wipe the autosave so the player gets a fresh state, not a stale chapter.
	UGameplayStatics::DeleteGameInSlot(UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex);

	// Reset input on the OUTGOING PC. Slate input mode lives on the
	// LocalPlayer and persists across OpenLevel — without this, the menu's
	// FInputModeUIOnly leaks into gameplay and freezes the player. The new
	// PC also resets in BeginPlay, but that fires too late on some builds.
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}

	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::OpenLevel(W, NewGameLevelName);
	}
}

void UEclipseMainMenuWidget::OnContinue()
{
	UE_LOG(LogEclipse, Log, TEXT("MainMenu: Continue"));

	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS)
	{
		if (StatusText) StatusText->SetText(FText::FromString(TEXT("State subsystem missing.")));
		return;
	}

	// TryLoadCurrent restores subsystem fields + queues a pending-teleport. The
	// player pawn will pick it up on its next BeginPlay in the target level.
	if (!GS->TryLoadCurrent())
	{
		if (StatusText) StatusText->SetText(FText::FromString(TEXT("No save game found.")));
		return;
	}

	// Same input-mode reset as OnNewGame — see comment there.
	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}

	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::OpenLevel(W, NewGameLevelName);
	}
}

void UEclipseMainMenuWidget::OnQuit()
{
	APlayerController* PC = GetOwningPlayer();
	UKismetSystemLibrary::QuitGame(GetWorld(), PC,
		EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}
