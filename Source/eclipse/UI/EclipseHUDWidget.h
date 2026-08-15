// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseHUDWidget.generated.h"

class UTextBlock;
class UImage;
class UBorder;
class UHorizontalBox;
class UProgressBar;

/**
 * Top-left HUD cluster — two life-meters (Heat, Thirst)
 * stacked vertically as continuous fill bars. Binds to
 * EclipseGameStateSubsystem::OnStateChanged. No backdrop panel — the
 * bars float directly over the game view.
 *
 * Per-meter layout (two lines):
 *   [LABEL]                                      [VALUE/MAX]
 *   [======================= bar =======================]
 *
 * The 0..10 sweet-spot model means BOTH extremes are bad — the bar
 * tints red instead of the meter's base color when the value is at or
 * past the critical-low/critical-high threshold.
 *
 * Per-meter base tints:
 *   HEAT          red
 *   THIRST        cyan
 *
 * Blueprint child names (BindWidgetOptional — populator names match):
 *   UProgressBar  HeatBar / ThirstBar
 *   UTextBlock    HeatValueText / ThirstValueText
 *   UTextBlock    HeatLabelText / ThirstLabelText
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

	// ── Life-meter bars ─────────────────────────────────────────────────
	// Continuous fill bars — UpdateBars sets Percent + FillColorAndOpacity
	// each time the meter changes.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HeatBar;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> ThirstBar;

	// Stat-name labels on the left of each row ("HEAT", "THIRST").
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HeatLabelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ThirstLabelText;

	// Integer-value labels on the right of each row (e.g. "7"). Updated
	// alongside the segments in UpdateBars.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> HeatValueText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ThirstValueText;

	// Center-screen crosshair dot — bound from the WBP designer if
	// present, otherwise built programmatically in Initialize().
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairImage;

	// Chapter clock readout — bottom-right of the HUD, deliberately large.
	// Runtime-injected in NativeConstruct if neither the WBP nor the C++
	// fallback tree provided one. The phone face shows the same value.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ChapterClockText;

	// Point size of the bottom-right clock, and its inset from the corner.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|HUD|Clock", meta = (ClampMin = "8"))
	int32 ClockFontSize = 48;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|HUD|Clock")
	float ClockMargin = 28.f;

	// Purple halo around the clock glyphs — fed to both the font outline
	// and a zero-offset drop shadow. Alpha'd well below 1 so it reads as
	// light bleeding off the edges rather than a hard stroke; 0 size
	// disables the glow.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|HUD|Clock", meta = (ClampMin = "0", ClampMax = "8"))
	int32 ClockGlowSize = 3;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|HUD|Clock")
	FLinearColor ClockGlowColor = FLinearColor(0.42f, 0.24f, 0.95f, 0.65f);

	// Plays whenever the DISPLAYED time changes — i.e. once per
	// ClockDisplayStepMinutes worth of choices, not on every choice.
	// Optional: PlayUI no-ops while this is unassigned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|HUD|Clock")
	TObjectPtr<class USoundBase> ClockTickSound;

	// Last string actually shown, so we only tick on a real change rather
	// than on every OnStateChanged broadcast (meters fire it constantly).
	// Empty until the first update, which suppresses a tick on open.
	FString LastClockDisplay;

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

	// Sets a bar's Percent + FillColorAndOpacity. Used by UpdateBars; pulled
	// out so each bar shares the critical-zone + pulse-flash math. Value is
	// 0..MeterMax; BaseTint is the meter's healthy-zone fill colour — swapped
	// for a red critical tint when Value is at/past the low or high
	// threshold. Pulse is a 0..1 "just changed" alpha — peaks at 1.0
	// immediately after a value change and decays to 0 over PulseDuration
	// seconds; the fill colour lerps toward white by that amount so the
	// change reads as a quick flash.
	void ApplyBarStyle(UProgressBar* Bar, int32 Value, FLinearColor BaseTint, float Pulse) const;

	// ── Pulse-on-change animation state ────────────────────────────────
	// Last seen meter values, so UpdateBars can detect "changed since last
	// broadcast" and trigger the pulse. -1 sentinel = "no last value yet";
	// the first UpdateBars call seeds these without flashing.
	int32 LastHeat        = -1;
	int32 LastThirst      = -1;

	// Per-meter pulse timer in [0, PulseDuration]. NativeTick decays these
	// toward 0; UpdateBars resets to PulseDuration on a value change.
	float HeatPulse        = 0.f;
	float ThirstPulse      = 0.f;

	// Time the flash takes to fade back to normal. Short enough to feel
	// like instant feedback, long enough that the eye catches it.
	static constexpr float PulseDuration = 0.4f;

	// (Kept for back-compat — formats the chapter clock from the shared
	// subsystem helper. HUD instance is collapsed so this is dormant
	// today.)
	void UpdateChapterClock();
	void UpdateCurrency();
};
