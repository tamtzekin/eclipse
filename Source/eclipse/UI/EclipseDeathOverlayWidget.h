// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseDeathOverlayWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Death overlay — opens when UEclipseGameStateSubsystem::OnPlayerDeath fires.
 * Modal pause + UIOnly input, two buttons:
 *   • TRY AGAIN — rewinds to last save (calls LoadLastSave on the subsystem)
 *   • QUIT      — returns to the main menu level
 *
 * Same visual language as the pause / stats menus: navy panel, cream/cyan
 * accents, BMSPA caps. Built via fallback tree when no WBP exists, or via
 * /Game/Justin/UI/WBP_DeathOverlay.WBP_DeathOverlay_C when a designer-styled
 * WBP is available.
 */
UCLASS()
class ECLIPSE_API UEclipseDeathOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseDeathOverlayWidget* OpenForPlayer(class APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> TryAgainBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> QuitBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Title;

private:
	UFUNCTION() void OnTryAgainClicked();
	UFUNCTION() void OnQuitClicked();

	void BuildFallbackTree();

	// One-shot guard to prevent double-firing if both buttons get clicked
	// before the level swap kicks in.
	bool bDismissed = false;
};
