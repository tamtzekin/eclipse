// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseEndScreenWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Subsystems/EclipseDemoFlow.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

void UEclipseEndArmer::HandleDialogueClosed()
{
	// One-shot: unsubscribe before opening, so an ordinary conversation
	// ending later can't raise a second end screen.
	UGameInstance* GI = Flow ? Flow->GetGameInstance() : nullptr;
	if (UEclipseDialogueSubsystem* DS = GI ? GI->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr)
	{
		DS->OnDialogueClosed.RemoveDynamic(this, &UEclipseEndArmer::HandleDialogueClosed);
	}
	if (Flow)
	{
		Flow->ShowEndScreen();
	}
	RemoveFromRoot();
}

void UEclipseEndScreenWidget::ArmFor(APlayerController* PC, UEclipseDemoFlow* Flow)
{
	if (!PC || !Flow) return;

	UEclipseDialogueSubsystem* DS = Flow->GetGameInstance()
		? Flow->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr;
	if (!DS) return;

	UEclipseEndArmer* Armer = NewObject<UEclipseEndArmer>(Flow);
	Armer->PC = PC;
	Armer->Flow = Flow;
	// Rooted because nothing else holds a reference until the delegate fires.
	Armer->AddToRoot();
	DS->OnDialogueClosed.AddDynamic(Armer, &UEclipseEndArmer::HandleDialogueClosed);
}

UEclipseEndScreenWidget* UEclipseEndScreenWidget::OpenForPlayer(APlayerController* PC, UEclipseDemoFlow* Flow)
{
	if (!PC) return nullptr;

	TSubclassOf<UEclipseEndScreenWidget> Cls = UEclipseEndScreenWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseEndScreenWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_EndScreen.WBP_EndScreen_C")))
	{
		Cls = BPClass;
	}

	UEclipseEndScreenWidget* W = CreateWidget<UEclipseEndScreenWidget>(PC, Cls, TEXT("EndScreen"));
	if (!W) return nullptr;

	W->DemoFlow = Flow;
	W->AddToViewport(/*ZOrder=*/2000);   // above the ending blackout and dialogue
	W->SetIsFocusable(true);

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	W->SetKeyboardFocus();

	UE_LOG(LogEclipse, Log, TEXT("EndScreen: shown"));
	return W;
}

bool UEclipseEndScreenWidget::Initialize()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transactional);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("ReplayBtn"))))
	{
		BuildFallbackTree();
	}
	return Super::Initialize();
}

void UEclipseEndScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ReplayBtn)
	{
		ReplayBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		ReplayBtn->OnClicked.AddDynamic(this, &UEclipseEndScreenWidget::OnReplayClicked);
	}
	if (QuitBtn)
	{
		QuitBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		QuitBtn->OnClicked.AddDynamic(this, &UEclipseEndScreenWidget::OnQuitClicked);
	}
}

void UEclipseEndScreenWidget::OnReplayClicked()
{
	if (bDismissed) return;
	bDismissed = true;
	RemoveFromParent();
	if (DemoFlow) DemoFlow->RestartDemo();
}

void UEclipseEndScreenWidget::OnQuitClicked()
{
	if (bDismissed) return;
	bDismissed = true;
	UWorld* W = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	RemoveFromParent();
	UKismetSystemLibrary::QuitGame(W, PC, EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UEclipseEndScreenWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Fully opaque: the ending already faded to black, and this keeps it
	// black even if the blackout underneath is torn down with the level.
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 1.f)));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("EndColumn"));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Column))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(420.f, 300.f));
		S->SetZOrder(1);
	}

	Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Title"));
	Title->SetText(FText::FromString(TEXT("ECLIPSE")));
	Title->SetFont(MakeBerenjena(64, 12.f));
	Title->SetColorAndOpacity(FSlateColor(DialogueRed));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 48.f));
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

	ReplayBtn = MakeBtn(TEXT("REPLAY"), TEXT("ReplayBtn"));
	QuitBtn   = MakeBtn(TEXT("QUIT"),   TEXT("QuitBtn"));
}
