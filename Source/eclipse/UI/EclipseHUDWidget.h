// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseHUDWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;
class UHorizontalBox;

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

	// Center-screen crosshair dot — bound from the WBP designer if present,
	// otherwise built programmatically in Initialize().
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	// Horizontal strip above the portrait+meters row that lists picked-up
	// items as small "chips". Built in Initialize() if the WBP doesn't
	// provide one. Mirrors the JS prototype's inventory-pill row.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> InventoryRibbon;

private:
	UFUNCTION()
	void HandleStateChanged();

	void UpdateBars();
	void UpdateInventory();
};
