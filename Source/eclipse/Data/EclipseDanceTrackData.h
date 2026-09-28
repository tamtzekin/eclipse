// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EclipseDanceStyle.h"
#include "EclipseDanceTrackData.generated.h"

class USoundWave;

// One danceable track and its beat grid; the grid is baked by Tools/analyse_dance_track.py.
UCLASS(BlueprintType)
class ECLIPSE_API UEclipseDanceTrackData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance")
	TObjectPtr<USoundWave> Sound;

	// From the kick-drum onsets; nudge by hand if the grid drifts off the kicks.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "60", ClampMax = "200"))
	float BPM = 128.f;

	// Track time of a bar's first beat (a "1"), the anchor every bar is counted from.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "0"))
	float FirstDownbeatSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "1"))
	int32 BeatsPerBar = 4;

	// Bar (counted from FirstDownbeatSeconds) where the battle's slice of the track begins.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "0"))
	int32 SegmentStartBar = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", meta = (ClampMin = "5"))
	float SegmentSeconds = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance")
	FText Title;

	// This track always runs as the tutorial: one style, the lessons, no switching.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance")
	bool bTutorialTrack = false;

	// Normalised loudness of the whole track, EnvelopeRate samples per second, for the scrolling waveform; editable so the analyser's Python can write it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", AdvancedDisplay)
	TArray<float> Envelope;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", AdvancedDisplay)
	float EnvelopeRate = 50.f;

	// The same track split into bass / mids / highs (0..1, EnvelopeRate per second): the waveform's layered shape.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", AdvancedDisplay) TArray<float> EnvelopeLow;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", AdvancedDisplay) TArray<float> EnvelopeMid;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dance", AdvancedDisplay) TArray<float> EnvelopeHigh;

	float BeatSeconds() const { return 60.f / FMath::Max(1.f, BPM); }
	float BarSeconds() const { return BeatsPerBar * BeatSeconds(); }
	float SegmentStartSeconds() const { return FirstDownbeatSeconds + SegmentStartBar * BarSeconds(); }
	int32 SegmentBars() const { return FMath::Max(1, FMath::RoundToInt(SegmentSeconds / BarSeconds())); }
};
