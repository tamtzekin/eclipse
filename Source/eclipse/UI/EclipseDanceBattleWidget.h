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

namespace EclipseDance
{
	// The colour a landed move flashes in.
	inline const FLinearColor Gold = FLinearColor(FColor(0xFF, 0xC8, 0x3D));
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

// Balance bar, Sekiro posture style: fills out from the centre as you drift from facing the opponent.
UCLASS()
class ECLIPSE_API UEclipseFacingGaugeWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetFacing(float InErrorDeg, float InWindowDeg) { ErrorDeg = InErrorDeg; WindowDeg = InWindowDeg; }
	void SetStance(float InPressure, bool bInStaggered) { Pressure = InPressure; bStaggered = bInStaggered; }

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	float ErrorDeg = 0.f;    // signed, 0 = facing them dead on
	float WindowDeg = 18.f;
	float Pressure = 0.f;    // 0..1 of your stance gone; 1 = broken
	bool bStaggered = false;
};

// Dance battle screen: his call, your answer, both balance bars and the per-style tint.
UCLASS()
class ECLIPSE_API UEclipseDanceBattleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shared by the C++ fallback and UEclipseUiBuilder::PopulateDanceBattleWBP so the two trees can't drift.
	static void BuildTree(UWidgetTree* Tree);

	// Retints the screen for the style now being danced (the opponent says its name).
	void ShowStyle(EEclipseDanceStyle Style);

	void ShowGrade(const FText& Grade, const FLinearColor& Color, float BeatSeconds = 0.f);

	// His balance bar rides above his head; pass the screen point, or hide it.
	void SetStanceScreenPos(const FVector2D& Pos, bool bVisible);

	// Your stance, 0..1, shown on the bottom bar: full means broken.
	void SetPlayerStance(float Ratio, bool bStaggered) { if (FacingGauge) FacingGauge->SetStance(Ratio, bStaggered); }

	// The name of a move you answered in full.
	void FlashChain(const FString& Name, const FLinearColor& Color);

	// Who you're up against and what they're expert in; shown until the scored battle starts.
	void ShowExpert(const FString& Who, EEclipseDanceStyle Style);

	void SetRadialSelected(EEclipseDanceStyle Style);
	void SetFacing(float ErrorDeg, float WindowDeg) { if (FacingGauge) FacingGauge->SetFacing(ErrorDeg, WindowDeg); }
	void SetFacingVisible(bool bVisible);

	// Opponent STANCE bar, 0..1; bHit shakes it.
	void SetStance(float Ratio, bool bHit);

	// His balance gives out: the bar flares white, cracks apart and falls away.
	void ShatterStance();

	// Edge glow in the given colour, flashed and fading across a beat (confirms a style pick).
	void Pulse(const FLinearColor& Color, float BeatSeconds, float Strength = 1.f);

	// The steady backbeat glow under any pick flash: 0..1, set every frame.
	void SetBackbeat(const FLinearColor& Color, float Level) { BackbeatColor = Color; BackbeatLevel = Level; }

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StyleTint;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StylePulse;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> GradeText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChainName;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ExpertText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StyleNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseFacingGaugeWidget> FacingGauge;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UProgressBar> OpponentStance;

	virtual void NativeConstruct() override;

private:
	float GradeFade = 0.f;
	float GradeBeat = 0.5f;
	float ChainFade = 0.f;
	float PulseA = 0.f;
	float PulseStrength = 1.f;
	float PulseBeatSeconds = 0.5f;
	FLinearColor BackbeatColor = FLinearColor::White;
	FLinearColor PulseColor = FLinearColor::White;
	float BackbeatLevel = 0.f;
	FLinearColor TintColor = FLinearColor::Transparent;
	FLinearColor TintTarget = FLinearColor::Transparent;

	float StanceShake = 0.f;
	float ShatterT = -1.f;   // <0 idle, else seconds into the break
};
