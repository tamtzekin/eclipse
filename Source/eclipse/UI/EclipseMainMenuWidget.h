// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseMainMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Boot-time / "Main Menu" overlay. Sits over an empty `L_MainMenu` level
 * showing NEW GAME / CONTINUE / QUIT. Spawned by AEclipseMainMenuActor on
 * BeginPlay; UI-only input mode with cursor.
 *
 *  - NEW GAME → wipes the autosave slot and opens L_Bathroom.
 *  - CONTINUE → reads ECLIPSE_AUTOSAVE; if it exists, loads its level and
 *    queues a pending-teleport so the player respawns where they left off.
 *  - QUIT     → UKismetSystemLibrary::QuitGame.
 */
UCLASS()
class ECLIPSE_API UEclipseMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Static helper: create + show the menu over the local player. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseMainMenuWidget* OpenForPlayer(class APlayerController* PC);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> NewGameBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ContinueBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> QuitBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;

	// Levels — soft pointers so the menu doesn't pull L_Bathroom into its own
	// cooked content. Default point at the slice's only level; designer can
	// override per-WBP later for branching content.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|UI")
	FName NewGameLevelName = TEXT("L_Bathroom");

private:
	UFUNCTION() void OnNewGame();
	UFUNCTION() void OnContinue();
	UFUNCTION() void OnQuit();

	void BuildFallbackTree();
	void RefreshContinueState();
};
