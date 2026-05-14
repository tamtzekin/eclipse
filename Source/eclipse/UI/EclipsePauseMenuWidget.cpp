// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePauseMenuWidget.h"
#include "Eclipse.h"
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

	// Helper that builds a button + label, parented to a given vertical box.
	auto MakeBtnIn = [&](UVerticalBox* Parent, const FString& Label, FName WidgetName,
		int32 FontSize = 56, TObjectPtr<UTextBlock>* OutLabel = nullptr) -> UButton*
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
		T->SetFont(MakeRodin(FontSize));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);
		if (OutLabel) *OutLabel = T;

		if (UVerticalBoxSlot* VS = Parent->AddChildToVerticalBox(Btn))
		{
			VS->SetPadding(FMargin(0.f, 14.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		return Btn;
	};

	// ── Main list: Resume / Save / Load / Main Menu / Quit ──
	MainList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MainList"));
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(MainList))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}
	ResumeBtn   = MakeBtnIn(MainList, TEXT("RESUME"),     TEXT("ResumeBtn"));
	SaveBtn     = MakeBtnIn(MainList, TEXT("SAVE"),       TEXT("SaveBtn"));
	LoadBtn     = MakeBtnIn(MainList, TEXT("LOAD"),       TEXT("LoadBtn"));
	MainMenuBtn = MakeBtnIn(MainList, TEXT("MAIN MENU"),  TEXT("MainMenuBtn"));
	QuitBtn     = MakeBtnIn(MainList, TEXT("QUIT"),       TEXT("QuitBtn"));

	// ── Slot picker: 3 slot rows + Back. Hidden until OnSave/OnLoad. ──
	SlotPicker = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SlotPicker"));
	SlotPicker->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(SlotPicker))
	{
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	SlotPickerTitle = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotPickerTitle"));
	SlotPickerTitle->SetText(FText::FromString(TEXT("SAVE GAME")));
	SlotPickerTitle->SetFont(MakeBMSPA(48, 8.f));
	SlotPickerTitle->SetColorAndOpacity(FSlateColor(Cyan));
	SlotPickerTitle->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = SlotPicker->AddChildToVerticalBox(SlotPickerTitle))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	// Slot row labels are filled in by RefreshSlotLabels() at runtime.
	Slot0Btn = MakeBtnIn(SlotPicker, TEXT("SLOT 1  ·  EMPTY"), TEXT("Slot0Btn"), 36, &Slot0Btn_Label);
	Slot1Btn = MakeBtnIn(SlotPicker, TEXT("SLOT 2  ·  EMPTY"), TEXT("Slot1Btn"), 36, &Slot1Btn_Label);
	Slot2Btn = MakeBtnIn(SlotPicker, TEXT("SLOT 3  ·  EMPTY"), TEXT("Slot2Btn"), 36, &Slot2Btn_Label);
	SlotBackBtn = MakeBtnIn(SlotPicker, TEXT("BACK"), TEXT("SlotBackBtn"), 40);

	// Status line — reports save/load result. Lives below both sub-states.
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

	if (Slot0Btn)    Slot0Btn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnSlot0);
	if (Slot1Btn)    Slot1Btn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnSlot1);
	if (Slot2Btn)    Slot2Btn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnSlot2);
	if (SlotBackBtn) SlotBackBtn->OnClicked.AddDynamic(this, &UEclipsePauseMenuWidget::OnSlotBack);

	// Force MouseDown click-method on each — single-click select like dialogue.
	UButton* AllBtns[] = { ResumeBtn, SaveBtn, LoadBtn, MainMenuBtn, QuitBtn,
	                       Slot0Btn, Slot1Btn, Slot2Btn, SlotBackBtn };
	for (UButton* B : AllBtns) if (B) B->SetClickMethod(EButtonClickMethod::MouseDown);

	// Start in main-list view; pre-fill slot labels so the picker is responsive
	// the first time the user opens it.
	ShowMainList();
	RefreshSlotLabels();
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

void UEclipsePauseMenuWidget::OnSave()
{
	ShowSlotPicker(/*bSaveMode=*/true);
}

void UEclipsePauseMenuWidget::OnLoad()
{
	ShowSlotPicker(/*bSaveMode=*/false);
}

void UEclipsePauseMenuWidget::OnSlot0() { HandleSlot(0); }
void UEclipsePauseMenuWidget::OnSlot1() { HandleSlot(1); }
void UEclipsePauseMenuWidget::OnSlot2() { HandleSlot(2); }
void UEclipsePauseMenuWidget::OnSlotBack() { ShowMainList(); }

void UEclipsePauseMenuWidget::HandleSlot(int32 SlotIndex)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) { SetStatus(TEXT("State subsystem missing.")); return; }

	if (bSaveMode)
	{
		const bool bOk = GS->SaveToSlot(SlotIndex);
		SetStatus(bOk ? FString::Printf(TEXT("Saved to slot %d."), SlotIndex + 1)
		             : FString::Printf(TEXT("Save to slot %d failed."), SlotIndex + 1));
		// Save: stay in the menu so the player can keep configuring; just
		// refresh labels and drop back to the main list.
		RefreshSlotLabels();
		ShowMainList();
		return;
	}

	// Load: applying the snapshot mutates the live subsystem. Once it succeeds
	// the player wants to be *back in the world* — not still staring at the
	// pause overlay. Close the menu (which also unpauses + restores game-only
	// input mode) so the load lands the player in the restored state.
	const bool bOk = GS->LoadFromSlot(SlotIndex);
	if (bOk)
	{
		UE_LOG(LogEclipse, Log, TEXT("PauseMenu: load slot %d → closing menu, returning to world"),
			SlotIndex + 1);
		Close();
		return;
	}
	SetStatus(FString::Printf(TEXT("Slot %d empty."), SlotIndex + 1));
	RefreshSlotLabels();
	ShowMainList();
}

void UEclipsePauseMenuWidget::ShowSlotPicker(bool bInSaveMode)
{
	bSaveMode = bInSaveMode;
	bSlotMode = true;
	if (MainList)        MainList->SetVisibility(ESlateVisibility::Collapsed);
	if (SlotPicker)      SlotPicker->SetVisibility(ESlateVisibility::Visible);
	if (SlotPickerTitle) SlotPickerTitle->SetText(FText::FromString(bSaveMode ? TEXT("SAVE GAME") : TEXT("LOAD GAME")));
	RefreshSlotLabels();
}

void UEclipsePauseMenuWidget::ShowMainList()
{
	bSlotMode = false;
	if (MainList)   MainList->SetVisibility(ESlateVisibility::Visible);
	if (SlotPicker) SlotPicker->SetVisibility(ESlateVisibility::Collapsed);
}

void UEclipsePauseMenuWidget::RefreshSlotLabels()
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	UTextBlock* Labels[] = { Slot0Btn_Label, Slot1Btn_Label, Slot2Btn_Label };
	UButton*    Btns[]   = { Slot0Btn,   Slot1Btn,   Slot2Btn   };
	for (int32 i = 0; i < 3; ++i)
	{
		const FEclipseSaveSlotInfo Info = GS->GetSlotInfo(i);
		if (Labels[i]) Labels[i]->SetText(FText::FromString(Info.DisplayLabel));
		// In Load mode, grey out empty slots so the player can't try to load nothing.
		if (Btns[i])
		{
			const bool bEnabled = bSaveMode || Info.bExists;
			Btns[i]->SetIsEnabled(bEnabled);
		}
	}
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

void UEclipsePauseMenuWidget::SetStatus(const FString& Msg)
{
	if (StatusText) StatusText->SetText(FText::FromString(Msg));
	UE_LOG(LogEclipse, Log, TEXT("PauseMenu: %s"), *Msg);
}
