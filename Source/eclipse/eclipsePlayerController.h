// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "eclipsePlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;

/**
 *  Basic PlayerController class for a third person game
 *  Manages input mappings + Esc-toggled pause menu binding.
 *
 *  Was UCLASS(abstract) per the UE template (which assumed a BP subclass would
 *  always be derived). We need it concrete because BP_EclipseGameMode points
 *  PlayerControllerClass directly at this C++ class — abstract classes can't
 *  be spawned at world-init, which is the "Failed to spawn player controller"
 *  symptom.
 */
UCLASS()
class AeclipsePlayerController : public APlayerController
{
	GENERATED_BODY()
	
protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category ="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Esc-key handler — toggles the pause menu overlay. */
	void TogglePauseMenu();

	/** I-key handler — toggles the inventory overlay. */
	void ToggleInventory();

	UPROPERTY()
	TObjectPtr<class UEclipsePauseMenuWidget> ActivePauseMenu;

	UPROPERTY()
	TObjectPtr<class UEclipseInventoryWidget> ActiveInventory;
};
