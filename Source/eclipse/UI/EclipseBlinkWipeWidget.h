// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseBlinkWipeWidget.generated.h"

class UBorder;

/**
 * Eye-shut / eye-open transition wipe — two black "eyelid" borders
 * anchored to the top and bottom edges that animate inward to meet in
 * the middle (close), and outward back to the edges (open).
 *
 * Total visible duration ~0.18 s. Used between menu screens where the
 * cut is jarring without it (pause-menu QUIT → main menu, LOAD →
 * gameplay level, etc.).
 *
 * Two-phase API because `OpenLevel` tears down the source world and
 * the widget along with it:
 *
 *   • `PlayClose(PC, OnClosed)` — fade in black in the source context.
 *     `OnClosed` fires the instant the screen is fully covered; that's
 *     when callers normally call `OpenLevel`.
 *   • `PlayOpen(PC)` — start fully black, fade out. Call from the
 *     destination level / menu's BeginPlay or NativeConstruct.
 *
 * For in-place transitions that don't swap levels (e.g. menu A → menu B
 * in the same world) `PlayFull(PC, OnHalfway)` does both phases in one
 * widget — close, fire callback, open.
 */
UCLASS()
class ECLIPSE_API UEclipseBlinkWipeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	DECLARE_DELEGATE(FOnBlinkPhase);

	/** Eye-shut phase. Calls OnClosed when fully covered. */
	static UEclipseBlinkWipeWidget* PlayClose(class APlayerController* PC, FOnBlinkPhase OnClosed = FOnBlinkPhase());

	/** Eye-open phase. Starts black, fades to clear, removes itself. */
	static UEclipseBlinkWipeWidget* PlayOpen(class APlayerController* PC);

	/** Close → halfway callback → open, all on one widget. Use only when the
	 *  source world survives the transition (no OpenLevel mid-blink). */
	static UEclipseBlinkWipeWidget* PlayFull(class APlayerController* PC, FOnBlinkPhase OnHalfway = FOnBlinkPhase());

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	// Full-screen black "eyelid" — opacity-driven (0=open/transparent,
	// 1=closed/opaque-black). Name kept for back-compat with the v1
	// two-border implementation; BottomEyelid is no longer used but
	// stays as a BindWidgetOptional UPROPERTY so existing WBPs (if any
	// authored an eyelid pair) don't fail to bind.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> TopEyelid;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder> BottomEyelid;

private:
	void BuildFallbackTree();
	void ApplyEyelidPhase(float Phase01);   // 0 = open, 1 = fully closed

	enum class EBlinkMode : uint8 { Close, Open, Full };
	EBlinkMode Mode = EBlinkMode::Close;

	// Phase progress in seconds. ApplyEyelidPhase maps it into a 0..1 cover.
	float Elapsed = 0.f;
	float CloseDuration = 0.18f;   // 180 ms close (fade to black)
	float OpenDuration  = 0.18f;   // 180 ms open  (fade from black)
	bool  bMidpointFired = false;
	bool  bFinished      = false;

	FOnBlinkPhase MidpointDelegate;
};
