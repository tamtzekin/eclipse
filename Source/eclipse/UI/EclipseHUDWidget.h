// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;

/**
 * Bottom-right HUD cluster — heat bar, portrait box, thirst bar.
 * Binds to EclipseGameStateSubsystem::OnStateChanged.
 *
 * Mirrors the HTML #hud-status layout:
 *   background: rgba(6,14,36,0.6)  border: #1a3a5c  blur
 *   Heat bar: vertical, blue (#143c8c → #51eefc)
 *   Thirst bar: vertical, cyan (#51eefc)
 *   Portrait: 90×112, cyan border
 *
 * Blueprint child must contain:
 *   UProgressBar  HeatBar     — FillType: BottomToTop
 *   UProgressBar  ThirstBar   — FillType: BottomToTop
 *   (portrait Image/Border optional — styled in BP)
 */
UCLASS()
class ECLIPSE_API UEclipseHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// Optional bind: built programmatically in Initialize() if WBP omits them.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HeatBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ThirstBar;

	// HP-style Energy bar. Red pulse while bIsBleedingEnergy (thirst at 0).
	// Designer-bindable; built into the fallback tree if WBP doesn't ship one.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> EnergyBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EnergyLabel;

	// Center-screen crosshair dot — bound from the WBP designer if present,
	// otherwise built programmatically in Initialize().
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	// Chapter clock readout — "CH 1 · 1:23". Updated on every NativeTick
	// because the clock advances continuously (the meters' 1Hz throttle
	// would feel choppy here). Designer-styleable in the WBP details panel.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChapterClockText;

	// Currency readout — "◆ 5  ▤ 100" (coins + notes). Updated on
	// OnStateChanged. Designer-styleable; built programmatically if absent.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CurrencyText;

private:
	UFUNCTION()
	void HandleStateChanged();

	UFUNCTION()
	void HandlePlayerDeath();

	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	void UpdateBars();
	void UpdateChapterClock();
	void UpdateCurrency();
};
