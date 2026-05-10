// Copyright Epic Games, Inc. All Rights Reserved.


#include "eclipsePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "eclipse.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "UI/EclipsePauseMenuWidget.h"
#include "UI/EclipseInventoryWidget.h"
#include "UI/EclipseMainMenuActor.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "InputCoreTypes.h"
#include "Components/InputComponent.h"
#include "EngineUtils.h"

void AeclipsePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// ── Reset gameplay input every BeginPlay. ──
	// Slate input mode lives on the LocalPlayer (not the PC) and persists
	// across OpenLevel — so a UI-only mode set by the main-menu / pause-menu
	// widget keeps the new gameplay PC's pawn frozen unless we explicitly
	// flip it back. Same for cursor state and ignore-input flags.
	//
	// EXCEPT in menu levels: if an AEclipseMainMenuActor exists in the world,
	// the level is going to immediately open a UI overlay and own input mode
	// itself — skip the gameplay reset there or we fight the menu for cursor
	// state and the player can't click buttons.
	if (IsLocalPlayerController())
	{
		bool bMenuLevel = false;
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AEclipseMainMenuActor> It(W); It; ++It)
			{
				bMenuLevel = true;
				break;
			}
		}

		if (!bMenuLevel)
		{
			FInputModeGameOnly Mode;
			SetInputMode(Mode);
			SetShowMouseCursor(false);
			SetIgnoreMoveInput(false);
			SetIgnoreLookInput(false);
			UE_LOG(LogEclipse, Log, TEXT("PC::BeginPlay — gameplay input reset"));
		}
		else
		{
			UE_LOG(LogEclipse, Log, TEXT("PC::BeginPlay — menu level detected, skipping input reset"));
		}
	}

	// only spawn touch controls on local player controllers
	if (SVirtualJoystick::ShouldDisplayTouchInterface() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(LogEclipse, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void AeclipsePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Contexts
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!SVirtualJoystick::ShouldDisplayTouchInterface())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}

		// Pause-menu toggle on Esc. Bound via legacy InputComponent (not
		// Enhanced Input) so we don't need to ship an extra IA asset for
		// one key. bExecuteWhenPaused = true so Esc can also CLOSE the menu
		// it just opened. The dialogue widget keeps its own Esc handler
		// (close-conversation) for when a dialogue is active — TogglePauseMenu
		// early-returns in that case.
		if (InputComponent)
		{
			// Esc → pause menu
			{
				FInputKeyBinding B(FInputChord(EKeys::Escape, false, false, false, false), IE_Pressed);
				B.bExecuteWhenPaused = true;
				B.KeyDelegate.GetDelegateForManualSet().BindUObject(
					this, &AeclipsePlayerController::TogglePauseMenu);
				InputComponent->KeyBindings.Add(B);
			}
			// I → inventory overlay
			{
				FInputKeyBinding B(FInputChord(EKeys::I, false, false, false, false), IE_Pressed);
				B.bExecuteWhenPaused = true;
				B.KeyDelegate.GetDelegateForManualSet().BindUObject(
					this, &AeclipsePlayerController::ToggleInventory);
				InputComponent->KeyBindings.Add(B);
			}
			UE_LOG(LogEclipse, Log, TEXT("PC: Esc + I bindings installed on %s (InputComponent=%p)"),
				*GetName(), (void*)InputComponent);
		}
		else
		{
			UE_LOG(LogEclipse, Warning, TEXT("PC: SetupInputComponent — InputComponent is null on %s"), *GetName());
		}
	}
}

void AeclipsePlayerController::TogglePauseMenu()
{
	UE_LOG(LogEclipse, Log, TEXT("PC: TogglePauseMenu fired"));

	// If a dialogue is active, let the dialogue widget keep handling Esc
	// (close-the-conversation) — pause menu only fires from gameplay.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			if (DS->IsDialogueOpen())
			{
				UE_LOG(LogEclipse, Log, TEXT("PC: TogglePauseMenu — dialogue open, deferring to dialogue widget"));
				return;
			}
		}
	}

	if (ActivePauseMenu && ActivePauseMenu->IsInViewport())
	{
		UE_LOG(LogEclipse, Log, TEXT("PC: TogglePauseMenu — closing existing menu"));
		ActivePauseMenu->Close();
		ActivePauseMenu = nullptr;
		return;
	}

	UE_LOG(LogEclipse, Log, TEXT("PC: TogglePauseMenu — opening menu"));
	ActivePauseMenu = UEclipsePauseMenuWidget::OpenForPlayer(this);
	UE_LOG(LogEclipse, Log, TEXT("PC: TogglePauseMenu — OpenForPlayer returned %s"),
		ActivePauseMenu ? TEXT("OK") : TEXT("nullptr"));
}

void AeclipsePlayerController::ToggleInventory()
{
	UE_LOG(LogEclipse, Log, TEXT("PC: ToggleInventory fired"));

	// Don't open while a dialogue is active — let the dialogue widget keep
	// its own input ownership. (Mirrors the pause-menu guard.)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			if (DS->IsDialogueOpen())
			{
				UE_LOG(LogEclipse, Log, TEXT("PC: ToggleInventory — dialogue open, ignoring"));
				return;
			}
		}
	}
	// Don't double-open over the pause menu either.
	if (ActivePauseMenu && ActivePauseMenu->IsInViewport())
	{
		UE_LOG(LogEclipse, Log, TEXT("PC: ToggleInventory — pause menu open, ignoring"));
		return;
	}

	if (ActiveInventory && ActiveInventory->IsInViewport())
	{
		ActiveInventory->Close();
		ActiveInventory = nullptr;
		return;
	}
	ActiveInventory = UEclipseInventoryWidget::OpenForPlayer(this);
}
