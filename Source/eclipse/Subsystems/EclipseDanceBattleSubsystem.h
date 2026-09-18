// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Sound/QuartzQuantizationUtilities.h"
#include "Data/EclipseDanceStyle.h"
#include "EclipseDanceBattleSubsystem.generated.h"

class UEclipseDanceTrackData;
class UEclipseDanceBattleWidget;
class UQuartzClockHandle;
class UAudioComponent;
class ACameraActor;
class ACharacter;
class APointLight;
class UPointLightComponent;
class AEclipseNpcCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEclipseDanceBeat, int32, Bar, int32, Beat);

// Runs a dance battle off a Quartz clock so styles and countdowns land on the audio thread's beat, not the frame clock.
UCLASS()
class ECLIPSE_API UEclipseDanceBattleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Faces the player off against Opponent; a tutorial names each upcoming style instead of only colour-coding it.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dance")
	bool StartBattle(UEclipseDanceTrackData* Track, AEclipseNpcCharacter* Opponent, bool bTutorial = false);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dance")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dance")
	bool IsRunning() const { return Track != nullptr; }

	// Bar is 0-based from the segment start, Beat is 1..BeatsPerBar.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dance")
	FEclipseDanceBeat OnBeat;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEclipseDanceBattleSubsystem, STATGROUP_Tickables); }

private:
	enum class EGrade : uint8 { Perfect, Good, TooEarly, TooSlow, Miss, Count };

	UFUNCTION()
	void HandleBeat(FName ClockName, EQuartzCommandQuantization QuantizationType, int32 NumBars, int32 Beat, float BeatFraction);

	UFUNCTION()
	void HandleCommandEvent(EQuartzCommandDelegateSubType EventType, FName Name);

	// Intro bars first (no style), then a new style every 4 or 8 bars, never the same one twice running.
	void BuildSchedule(int32 NumBars);
	void GradeSwitch(int32 Bar);
	void RollOpponentSwitch(int32 Bar);
	void Finish();
	void EnterBattleCamera();
	void LeaveBattleCamera();
	void TickCamera(float DeltaTime);
	void TickDancers(float Now, float DeltaTime);
	float SongSeconds(double Now) const;

	UPROPERTY() TObjectPtr<UEclipseDanceTrackData> Track;
	UPROPERTY() TObjectPtr<AEclipseNpcCharacter> Opponent;
	UPROPERTY() TObjectPtr<UQuartzClockHandle> Clock;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	UPROPERTY() TObjectPtr<ACameraActor> BattleCamera;
	UPROPERTY() TObjectPtr<UEclipseDanceBattleWidget> Widget;
	UPROPERTY() TObjectPtr<APointLight> StageLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> PlayerHalo;

	TArray<EEclipseDanceStyle> Schedule;   // Count = intro bar, nobody dances yet
	TArray<FString> IntroLines;
	int32 OpponentLevel[(int32)EEclipseDanceStyle::Count] = {};

	// Beat timing, in FPlatformTime seconds, as delivered by Quartz.
	double LastBeatTime = 0.0;
	int32 LastBar = -1;
	int32 LastBeat = 0;
	double SwitchTime = 0.0;              // downbeat of the most recent switch bar

	EEclipseDanceStyle PlayerStyle = EEclipseDanceStyle::Count;
	EEclipseDanceStyle OpponentStyle = EEclipseDanceStyle::Count;
	double StyleChosenAt = 0.0;
	int32 Score = 0;
	int32 OpponentScore = 0;
	int32 Counts[(int32)EGrade::Count] = {};
	bool bOpponentHit = false;
	float Performance = 0.5f;             // running 0..1 grade average; drives how hard the camera punches in
	bool bFinished = false;

	// Camera: a two-shot that orbits a little and tilts to alternating sides on every switch, with a quick punch.
	FVector CamMid = FVector::ZeroVector;
	FVector CamOffset = FVector::ZeroVector;
	float CamRoll = 0.f;
	float CamRollTarget = 0.f;
	float CamOrbit = 0.f;
	float CamOrbitTarget = 0.f;
	float CamBump = 1.f;                  // 0..1 through the bump; 1 = idle

	float HaloFlash = 0.f;                // 1 on a style change, fades to the steady dancing glow
	float BeatFlash = 0.f;

	TMap<TWeakObjectPtr<ACharacter>, FTransform> MeshBase;   // restored on Stop
};
