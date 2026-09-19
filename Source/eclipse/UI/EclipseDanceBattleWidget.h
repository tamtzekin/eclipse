// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/EclipseDanceStyle.h"
#include "EclipseDanceBattleWidget.generated.h"

class UTextBlock;
class UImage;
class UVerticalBox;
class UButton;
class UProgressBar;
class UWidget;
class UWidgetTree;
class UEclipseDanceTrackData;

// One line of the results board.
struct FEclipseDanceResultRow
{
	FString Label;
	int32 Value = 0;
	FString Text;         // shown as-is instead of a counted number (e.g. the RATING)
	FLinearColor Color = FLinearColor::White;
	bool bGlow = false;   // platinum: pulses bright white
};

namespace EclipseDance
{
	// Medal colours shared by the in-battle grades and the results board.
	inline const FLinearColor Bronze   = FLinearColor(FColor(0xCD, 0x7F, 0x32));
	inline const FLinearColor Silver   = FLinearColor(FColor(0xC8, 0xD0, 0xDA));
	inline const FLinearColor Gold     = FLinearColor(FColor(0xFF, 0xC8, 0x3D));
	inline const FLinearColor Platinum = FLinearColor(1.f, 1.f, 1.f);

	// Rating for a score as a fraction of the best possible, in the grade words and their medal colours; PERFECT glows.
	inline void Tier(float Ratio, FString& Name, FLinearColor& Color, bool& bGlow)
	{
		bGlow = Ratio >= 0.85f;
		if (bGlow)               { Name = TEXT("PERFECT");   Color = Platinum; }
		else if (Ratio >= 0.65f) { Name = TEXT("GOOD");      Color = Gold; }
		else if (Ratio >= 0.4f)  { Name = TEXT("TOO SLOW");  Color = Silver; }
		else                     { Name = TEXT("TOO EARLY"); Color = Bronze; }
	}
}

// Rekordbox-style wave: the track's loudness scrolling right to left past a centre playhead, style switches glowing in colour.
UCLASS()
class ECLIPSE_API UEclipseWaveformWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetTrack(UEclipseDanceTrackData* InTrack, const TArray<TPair<float, EEclipseDanceStyle>>& InSwitches);
	void SetPlayhead(float SongSeconds) { Playhead = SongSeconds; }
	void SetPump(float InPump) { Pump = InPump; }   // 0 at rest, 1 on a beat, more on a style switch; swells the whole wave
	void SetPresence(float InPresence) { Presence = InPresence; }   // 0..1: height and opacity, for the spin-up
	void SetHot(bool bInHot) { bHot = bInHot; }                      // HEAT mode: everything burns brighter

	// Seconds of track visible across the strip's width.
	UPROPERTY(EditAnywhere, Category = "Dance")
	float WindowSeconds = 10.f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY() TObjectPtr<UEclipseDanceTrackData> Track;
	TArray<TPair<float, EEclipseDanceStyle>> Switches;   // song time of each switch downbeat
	float Playhead = 0.f;
	float Pump = 0.f;
	float Presence = 1.f;
	bool bHot = false;
};

// Facing gauge: a short arc with a lit centre notch; the marker slides off it as you drift from facing the opponent.
UCLASS()
class ECLIPSE_API UEclipseFacingGaugeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetFacing(float InErrorDeg, float InWindowDeg) { ErrorDeg = InErrorDeg; WindowDeg = InWindowDeg; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float ErrorDeg = 0.f;    // signed, 0 = facing them dead on
	float WindowDeg = 18.f;
};

// Resident Evil-style equip wheel: one ring segment per style, the held one lit and named in the middle.
UCLASS()
class ECLIPSE_API UEclipseStyleWheelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSelected(EEclipseDanceStyle Style) { Selected = Style; SelectPunch = 1.f; }
	void SetUnlocked(int32 InMask) { UnlockedMask = InMask; }   // locked slots draw dark and unnamed

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EEclipseDanceStyle Selected = EEclipseDanceStyle::Count;   // Count = nothing held yet
	float SelectPunch = 0.f;
	int32 UnlockedMask = ~0;
};

// Dance battle screen: countdown, grades, style wheel, per-style tint and the results board.
UCLASS()
class ECLIPSE_API UEclipseDanceBattleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shared by the C++ fallback and UEclipseUiBuilder::PopulateDanceBattleWBP so the two trees can't drift.
	static void BuildTree(UWidgetTree* Tree);

	// Retints the screen for the style now being danced (the opponent says its name).
	void ShowStyle(EEclipseDanceStyle Style);

	// Count 4..1 before a switch, in Next's colour; 0 hides it. 3, 2 and 1 pulse.
	void ShowCountdown(int32 Count, EEclipseDanceStyle Next, float BeatSeconds);

	void ShowGrade(const FText& Grade, const FLinearColor& Color);

	void SetRadialSelected(EEclipseDanceStyle Style);
	void SetFacing(float ErrorDeg, float WindowDeg) { if (FacingGauge) FacingGauge->SetFacing(ErrorDeg, WindowDeg); }

	// Battle HEAT bar: fills with GOOD/PERFECT; at the top it glows for HEAT mode.
	void SetHeat(int32 Heat, bool bHeatMode);

	// Opponent STANCE bar, 0..1; bHit shakes it.
	void SetStance(float Ratio, bool bHit);
	void SetUnlockedStyles(int32 Mask);

	// Fired by CONTINUE on the results board.
	FSimpleDelegate OnContinue;

	// Edge glow in the given colour, flashed and fading across a beat (confirms a style pick).
	void Pulse(const FLinearColor& Color, float BeatSeconds, float Strength = 1.f);

	// The steady backbeat glow under any pick flash: 0..1, set every frame.
	void SetBackbeat(const FLinearColor& Color, float Level) { BackbeatColor = Color; BackbeatLevel = Level; }

	// Arcade tally: each row counts up from zero with a ticking sound, one after another, then the verdict lands.
	void ShowResults(const TArray<FEclipseDanceResultRow>& Rows, const FString& Verdict, const FLinearColor& VerdictColor, bool bVerdictGlow);

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StyleTint;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StylePulse;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> GradeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CountNumber;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseStyleWheelWidget> StyleWheel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseFacingGaugeWidget> FacingGauge;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> BattleHeat;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BattleHeatLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> OpponentStance;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ResultsPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> ResultsBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> VerdictText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ContinueBtn;

	UFUNCTION() void HandleContinueClicked() { OnContinue.ExecuteIfBound(); }
	virtual void NativeConstruct() override;

private:
	float CountPunch = 0.f;        // 1 on a counted beat, eases to 0 over it
	float CountBeatSeconds = 0.5f;
	float GradeFade = 0.f;
	float PulseA = 0.f;
	float PulseStrength = 1.f;
	float PulseBeatSeconds = 0.5f;
	FLinearColor BackbeatColor = FLinearColor::White;
	FLinearColor PulseColor = FLinearColor::White;
	float BackbeatLevel = 0.f;
	FLinearColor TintColor = FLinearColor::Transparent;
	FLinearColor TintTarget = FLinearColor::Transparent;

	struct FResultRow { TObjectPtr<UTextBlock> Value; int32 Target = 0; int32 Shown = -1; float Start = 0.f; float Count = 0.4f; bool bGlow = false; };
	TArray<FResultRow> ResultRows;
	float ResultsClock = -1.f;     // <0 while the tally isn't up
	float VerdictAt = 0.f;
	float VerdictPunch = 0.f;
	bool bVerdictGlow = false;
	bool bHeatMode = false;
	float HeatGlowT = 0.f;
	float StanceShake = 0.f;
	float LastTick = -1.f;
	UPROPERTY() TObjectPtr<class USoundBase> TallyTick;
};
