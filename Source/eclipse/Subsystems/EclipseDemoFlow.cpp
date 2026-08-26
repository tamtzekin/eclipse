// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDemoFlow.h"
#include "Eclipse.h"
#include "EclipseDemoSettings.h"
#include "EclipseDialogueSubsystem.h"
#include "EclipseGameStateSubsystem.h"
#include "Save/EclipseSaveGame.h"
#include "UI/EclipseEndScreenWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Console commands — so the demo framing is testable in PIE without cooking
//  a build and without having to play all the way to the end condition.
//
//    Eclipse.Demo.End        full sequence: fade to black, ending knot, end screen
//    Eclipse.Demo.EndScreen  jump straight to the REPLAY / QUIT screen
//
//  (The START screen has its own switch instead: Project Settings -> Game ->
//   Eclipse Demo -> "Show Start Screen In PIE".)
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	UEclipseDemoFlow* FlowFor(UWorld* World)
	{
		UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UEclipseDemoFlow>() : nullptr;
	}

	FAutoConsoleCommandWithWorld GTriggerEndingCmd(
		TEXT("Eclipse.Demo.End"),
		TEXT("Run the demo ending: fade to black, play the ending knot, then the end screen."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UEclipseDemoFlow* Flow = FlowFor(World))
			{
				Flow->TriggerEnding();
			}
			else
			{
				UE_LOG(LogEclipse, Warning, TEXT("Eclipse.Demo.End: no DemoFlow (not in a game world?)"));
			}
		}));

	FAutoConsoleCommandWithWorld GShowEndScreenCmd(
		TEXT("Eclipse.Demo.EndScreen"),
		TEXT("Show the REPLAY / QUIT end screen immediately, skipping the ending dialogue."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UEclipseDemoFlow* Flow = FlowFor(World))
			{
				Flow->ShowEndScreen();
			}
		}));
}

void UEclipseDemoFlow::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 1 Hz. The trigger is a quest going active, not a frame-accurate event,
	// so anything faster is wasted work — and the ending is a fade, which
	// hides up to a second of latency completely.
	PollHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UEclipseDemoFlow::PollEndCondition), 1.0f);
}

void UEclipseDemoFlow::Deinitialize()
{
	if (PollHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PollHandle);
		PollHandle.Reset();
	}
	Super::Deinitialize();
}

bool UEclipseDemoFlow::ShouldShowStartScreen() const
{
	const UEclipseDemoSettings& S = UEclipseDemoSettings::Get();
	if (!S.bDemoFlowEnabled) return false;

#if WITH_EDITOR
	// PIE. Skip by default so pressing Play drops straight into the level
	// being edited instead of making you click through the menu every time.
	if (GIsEditor)
	{
		return S.bShowStartScreenInPIE;
	}
#endif
	return true;   // packaged build
}

bool UEclipseDemoFlow::PollEndCondition(float /*DeltaSeconds*/)
{
	if (bEnding) return true;

	const UEclipseDemoSettings& S = UEclipseDemoSettings::Get();
	if (!S.bDemoFlowEnabled || S.EndTriggerQuest.IsNone()) return true;

	UGameInstance* GI = GetGameInstance();
	UEclipseDialogueSubsystem* DS = GI ? GI->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr;
	if (!DS) return true;

	// Don't cut away mid-conversation — the beat that sets the quest is
	// usually the last line of a dialogue, and yanking to black before the
	// player has read it loses the payoff.
	if (DS->IsDialogueOpen()) return true;

	// GetActiveSideQuests returns display lines (quest_text), not raw names,
	// so match on the raw LIST via the same subsystem the HUD uses. The
	// display text is what quest_text() rewrites it to, hence the fallback
	// substring check on the raw id.
	for (const FString& Line : DS->GetActiveSideQuests())
	{
		if (Line.Contains(S.EndTriggerQuest.ToString(), ESearchCase::IgnoreCase))
		{
			TriggerEnding();
			return true;
		}
	}
	return true;   // keep ticking
}

void UEclipseDemoFlow::TriggerEnding()
{
	if (bEnding) return;
	bEnding = true;

	const UEclipseDemoSettings& S = UEclipseDemoSettings::Get();

	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogEclipse, Warning, TEXT("DemoFlow: ending triggered with no PlayerController"));
		return;
	}

	// Full-screen black, under the dialogue panel but over everything else.
	// Built here rather than as its own widget class: it is one opaque
	// rectangle, and a whole WBP for that would be ceremony.
	if (UUserWidget* Black = CreateWidget<UUserWidget>(PC, UUserWidget::StaticClass(), TEXT("EndingBlackout")))
	{
		if (!Black->WidgetTree)
		{
			Black->WidgetTree = NewObject<UWidgetTree>(Black, TEXT("WidgetTree"), RF_Transactional);
		}
		UCanvasPanel* Root = Black->WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		Black->WidgetTree->RootWidget = Root;

		UBorder* Fill = Black->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Blackout"));
		FSlateBrush B;
		B.TintColor = FSlateColor(FLinearColor::Black);
		Fill->SetBrush(B);
		if (UCanvasPanelSlot* CS = Root->AddChildToCanvas(Fill))
		{
			CS->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			CS->SetOffsets(FMargin(0.f));
		}
		Black->AddToViewport(/*ZOrder=*/800);   // dialogue sits at 900+
	}

	// The ending plays as an ordinary dialogue with no speaker — its `-> END`
	// closes the dialogue, and OnDialogueClosed is what raises the end
	// screen (wired by UEclipseEndScreenWidget::ArmFor).
	UEclipseEndScreenWidget::ArmFor(PC, this);

	if (UEclipseDialogueSubsystem* DS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr)
	{
		if (!DS->OpenKnot(S.EndingDialogueKnot))
		{
			// No such knot authored yet — go straight to the end screen
			// rather than stranding the player on a black rectangle.
			UE_LOG(LogEclipse, Warning,
				TEXT("DemoFlow: ending knot '%s' missing — skipping to end screen"),
				*S.EndingDialogueKnot.ToString());
			ShowEndScreen();
		}
	}
	UE_LOG(LogEclipse, Log, TEXT("DemoFlow: ending triggered"));
}

void UEclipseDemoFlow::ShowEndScreen()
{
	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC) return;
	UEclipseEndScreenWidget::OpenForPlayer(PC, this);
}

void UEclipseDemoFlow::RestartDemo()
{
	const UEclipseDemoSettings& S = UEclipseDemoSettings::Get();

	// Same reset the main menu's NEW GAME performs: wipe the autosave so the
	// replay starts clean rather than resuming the run that just ended.
	UGameplayStatics::DeleteGameInSlot(UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex);

	bEnding = false;

	// Clear the end screen's UI input mode off the outgoing controller —
	// it lives on the LocalPlayer and survives OpenLevel, so leaving it set
	// freezes the player on arrival (see UEclipseMainMenuWidget::OnNewGame).
	UWorld* W = GetWorld();
	if (APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}

	if (W)
	{
		UGameplayStatics::OpenLevel(W, S.StartLevel);
	}
}
