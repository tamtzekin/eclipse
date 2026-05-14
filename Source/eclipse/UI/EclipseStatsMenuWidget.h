// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseStatsMenuWidget.generated.h"

class UTextBlock;
class UButton;
class UProgressBar;
class UImage;

/**
 * Stats panel — counterpart to the inventory overlay.
 * Toggled with `C` (character), freezes world input, displays:
 *   • Aesthetics / Stimulation / Rhythm / Zen / Psychedelics  (5 stats)
 *   • Heat / Thirst meters (live readout)
 *   • Player portrait
 *   • Currency (◆ coins / ▤ notes)
 *   • Close button
 *
 * Lives at /Game/Justin/UI/WBP_StatsMenu when a designer-styled WBP exists,
 * otherwise builds a minimal C++ fallback layout in Initialize().
 */
UCLASS()
class ECLIPSE_API UEclipseStatsMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseStatsMenuWidget* OpenForPlayer(class APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// One label per stat — both name + value rendered as a single text block
	// per row so designers can re-style the row with one font edit.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> AestheticsRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StimulationRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> RhythmRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ZenRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PsychedelicsRow;

	// Meter rows.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>   HeatRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>   ThirstRow;

	// Currency readout (e.g. "◆ 12   ▤ 3").
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>   CurrencyRow;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>      CloseBtn;

private:
	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void HandleStateChanged();

	void BuildFallbackTree();
	void RefreshAll();

	// Re-entry guard so the PC C-binding and the widget's NativeOnKeyDown
	// can't both run the full close path on the same press.
	bool bClosing = false;
};
