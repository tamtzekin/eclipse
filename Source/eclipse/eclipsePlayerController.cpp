// Copyright Epic Games, Inc. All Rights Reserved.


#include "eclipsePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "eclipse.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "UI/EclipsePauseMenuWidget.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "InputCoreTypes.h"
#include "Components/InputComponent.h"

void AeclipsePlayerController::BeginPlay()
{
	Super::BeginPlay();

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
			FInputKeyBinding B(FInputChord(EKeys::Escape, false, false, false, false), IE_Pressed);
			B.bExecuteWhenPaused = true;
			B.KeyDelegate.GetDelegateForManualSet().BindUObject(
				this, &AeclipsePlayerController::TogglePauseMenu);
			InputComponent->KeyBindings.Add(B);
			UE_LOG(LogEclipse, Log, TEXT("PC: Esc binding installed on %s (InputComponent=%p)"),
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
