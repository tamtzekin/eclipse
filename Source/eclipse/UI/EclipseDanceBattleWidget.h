// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/EclipseDanceStyle.h"
#include "EclipseDanceBattleWidget.generated.h"

class UTextBlock;
class UImage;
class UVerticalBox;
class UWidget;
class UWidgetTree;
class UEclipseDanceTrackData;

// Rekordbox-style strip: the track's loudness scrolling right to left past a centre playhead, style switches marked in colour.
UCLASS()
class ECLIPSE_API UEclipseWaveformWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetTrack(UEclipseDanceTrackData* InTrack, const TArray<TPair<float, EEclipseDanceStyle>>& InSwitches);
	void SetPlayhead(float SongSeconds) { Playhead = SongSeconds; }

	// Seconds of track visible across the strip's width.
	UPROPERTY(EditAnywhere, Category = "Dance")
	float WindowSeconds = 8.f;

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	UPROPERTY() TObjectPtr<UEclipseDanceTrackData> Track;
	TArray<TPair<float, EEclipseDanceStyle>> Switches;   // song time of each switch downbeat
	float Playhead = 0.f;
};

// Resident Evil-style equip wheel: one ring segment per style, the held one lit and named in the middle.
UCLASS()
class ECLIPSE_API UEclipseStyleWheelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSelected(EEclipseDanceStyle Style) { Selected = Style; SelectPunch = 1.f; }

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EEclipseDanceStyle Selected = EEclipseDanceStyle::Count;   // Count = nothing held yet
	float SelectPunch = 0.f;
};

// Top-of-screen dance battle readout: style, countdown to the next one, grades, and the style wheel.
UCLASS()
class ECLIPSE_API UEclipseDanceBattleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Shared by the C++ fallback and UEclipseUiBuilder::PopulateDanceBattleWBP so the two trees can't drift.
	static void BuildTree(UWidgetTree* Tree);

	void ShowStyle(EEclipseDanceStyle Style);

	// Count 4..1 before a switch; 0 hides the countdown. 3, 2 and 1 pulse.
	void ShowCountdown(int32 Count, EEclipseDanceStyle Next, float BeatSeconds);

	void ShowGrade(const FText& Grade, const FLinearColor& Color);

	void SetRadialSelected(EEclipseDanceStyle Style);

	// Tutorial names the upcoming style; otherwise NEXT: is just tinted in its colour.
	void SetShowNames(bool bInShowNames) { bShowNames = bInShowNames; }

	// Edge glow in the current style's colour, flashed on the beat and fading across it.
	void Pulse(const FLinearColor& Color, float BeatSeconds);

	// Arcade tally: rows count up one after another, then the verdict lands. Returns how long that takes.
	float ShowResults(const TArray<TPair<FString, int32>>& Rows, const FString& Verdict, const FLinearColor& VerdictColor);

	UEclipseWaveformWidget* GetWaveform() const { return Waveform; }

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StyleText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> CountdownBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NextLabel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NextStyle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> CountNumber;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> GradeText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseWaveformWidget> Waveform;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseStyleWheelWidget> StyleWheel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UImage> StylePulse;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> ResultsPanel;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> ResultsBox;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> VerdictText;

private:
	float StylePunch = 0.f;    // 1 on a switch, eases to 0: the style name pops then settles
	float CountPunch = 0.f;     // same for the countdown number, over one beat
	float CountBeatSeconds = 0.5f;
	float GradeFade = 0.f;
	float PulseA = 0.f;
	float PulseBeatSeconds = 0.5f;
	bool bShowNames = false;

	struct FResultRow { TObjectPtr<UTextBlock> Value; int32 Target = 0; int32 Shown = -1; };
	TArray<FResultRow> ResultRows;
	float ResultsClock = -1.f;     // <0 while the tally isn't up
	float VerdictPunch = 0.f;
};
