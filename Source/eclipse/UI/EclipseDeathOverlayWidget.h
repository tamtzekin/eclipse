// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseDeathOverlayWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * "YOU BLACKED OUT" overlay — opens when UEclipseGameStateSubsystem::
 * OnPlayerDeath fires (Heat and Thirst both 0). Modal pause, two buttons:
 *   • RETRY — rewinds to save slot 0, or resets the meters if there isn't one
 *   • QUIT  — returns to the main menu level
 *
 * The frozen frame behind it is blurred and vignetted, so the night reads as
 * slipping away rather than cutting to a menu. Built via fallback tree when no WBP exists, or via
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
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> TryAgainBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> QuitBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Title;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SavedText;   // "Last saved: 2 minutes ago"

private:
	UFUNCTION() void OnTryAgainClicked();
	UFUNCTION() void OnQuitClicked();

	void BuildFallbackTree();

	// One-shot guard to prevent double-firing if both buttons get clicked
	// before the level swap kicks in.
	bool bDismissed = false;

	float FadeT = 0.f;   // 0..1 fade into the passed-out screen
};
