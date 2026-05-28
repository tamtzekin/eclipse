// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseHUDWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class UHorizontalBox;

/**
 * Top-left HUD cluster — three life-meters (Heat, Thirst, Stimulation)
 * stacked vertically as horizontal segmented bars. Binds to
 * EclipseGameStateSubsystem::OnStateChanged.
 *
 * Per-row layout: [LABEL]  [10 segments + 2 dotted dividers]  [VALUE]
 *
 * Each meter is rendered as a horizontal row of 10 UBorder "segments"
 * (filling left→right) so the player can read the integer value at a
 * glance. The 0..10 sweet-spot model means BOTH extremes are bad —
 * segments at indices 0/1 (critical low, left edge) and 8/9 (critical
 * high, right edge) render red instead of the meter's base tint when
 * lit. A thin dotted-line divider sits between segments 1-2 and 7-8 to
 * mark the critical-zone boundaries.
 *
 * Per-meter base tints:
 *   HEAT          red
 *   THIRST        cyan
 *   STIMULATION   yellow-white
 *
 * All three bars share identical dimensions — a key UX requirement
 * since the game revolves around balancing these meters via consumables
 * + dialogue effects, and side-by-side comparison must be instant.
 *
 * Blueprint child names (BindWidgetOptional — populator names match):
 *   UHorizontalBox  HeatSegmentRow        (parent of HeatSeg_0..9 + dividers)
 *   UHorizontalBox  ThirstSegmentRow      (likewise)
 *   UHorizontalBox  StimulationSegmentRow (likewise)
 *   UTextBlock      HeatValueText / ThirstValueText / StimulationValueText
 *   UTextBlock      HeatLabelText / ThirstLabelText / StimulationLabelText
 */
UCLASS()
class ECLIPSE_API UEclipseHUDWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	// ── Life-meter segment containers ──────────────────────────────────
	// The WBP populator fills each row with 10 child segments + 2 dotted
	// dividers (named HeatSeg_0..HeatSeg_9 etc., left→right). At runtime
	// UpdateBars walks the children + tints each one based on the meter
	// value.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> HeatSegmentRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> ThirstSegmentRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> StimulationSegmentRow;

	// Stat-name labels on the left of each row ("HEAT", "THIRST", "STIMULATION").
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HeatLabelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ThirstLabelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StimulationLabelText;

	// Integer-value labels on the right of each row (e.g. "7"). Updated
	// alongside the segments in UpdateBars.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HeatValueText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ThirstValueText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StimulationValueText;

	// Center-screen crosshair dot — bound from the WBP designer if
	// present, otherwise built programmatically in Initialize().
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	// Chapter clock readout — kept on the HUD class so a future design
	// can toggle it back on. Today the actual readout lives on the phone
	// face (UEclipsePhoneWidget); this widget's instance is collapsed at
	// NativeConstruct time.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChapterClockText;

	// Currency readout — same as above, lives on the phone face now.
	// HUD instance collapsed at NativeConstruct.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CurrencyText;

private:
	UFUNCTION()
	void HandleStateChanged();

	UFUNCTION()
	void HandlePlayerDeath();

	// Repaints all three bars based on the current GameState meter values
	// and the integer-value labels above each.
	void UpdateBars();

	// Tints a single bar's children. Used by UpdateBars; pulled out so
	// each bar can share the segment-tinting math + the critical-zone
	// override logic. SegmentRow is the UHorizontalBox; Value is 0..10;
	// BaseTint is the meter's healthy-zone fill colour.
	void TintBar(UHorizontalBox* SegmentRow, int32 Value, FLinearColor BaseTint) const;

	// (Kept for back-compat — formats the chapter clock from the shared
	// subsystem helper. HUD instance is collapsed so this is dormant
	// today.)
	void UpdateChapterClock();
	void UpdateCurrency();
};
