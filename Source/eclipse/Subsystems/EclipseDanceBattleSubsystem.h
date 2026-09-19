// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/EclipseDanceStyle.h"
#include "EclipseDanceBattleSubsystem.generated.h"

class UEclipseDanceTrackData;
class UEclipseDanceBattleWidget;
class UEclipseWaveformWidget;
class APlayerController;
class UUserWidget;
class USoundWave;
class UAudioComponent;
class ACameraActor;
class ACharacter;
class APointLight;
class UPointLightComponent;
class AEclipseNpcCharacter;

// Runs a dance battle off the song's own playback position, so styles and countdowns land where the beat is heard.
UCLASS()
class ECLIPSE_API UEclipseDanceBattleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// Faces the player off against Opponent: intro, a demo of each unlocked style, then the scored battle.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dance")
	bool StartBattle(UEclipseDanceTrackData* Track, AEclipseNpcCharacter* Opponent);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dance")
	void Stop();

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dance")
	bool IsRunning() const { return Track != nullptr; }

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UEclipseDanceBattleSubsystem, STATGROUP_Tickables); }

private:
	enum class EGrade : uint8 { Perfect, Good, TooEarly, TooSlow, Miss, Hot, Count };

	// Bar is 0-based from the segment start, Beat 1..BeatsPerBar; fired from Tick as the song reaches each beat.
	void HandleBeat(int32 Bar, int32 Beat);
	void HandlePlaybackPercent(const UAudioComponent* Comp, const USoundWave* PlayingWave, const float Percent);

	// Intro bars (no style), a 4-bar demo of each unlocked style, then a new unlocked style every 4 or 8 bars.
	void BuildSchedule();
	void GradeSwitch(int32 Bar);
	void RollOpponentSwitch(int32 Bar);
	void Finish();
	void EnterBattleCamera();
	void LeaveBattleCamera();
	void TickCamera(float DeltaTime);
	void TickDancers(float Now, float DeltaTime);
	void TickMusic(float DeltaTime);
	void TickStrafe(APlayerController* PC, float DeltaTime);
	void SpawnWaveWall();
	void SetGameUiHidden(bool bHidden);
	float SongSeconds(double Now) const;
	float DeckSpeed() const;   // the pause-menu slow-down/spin-up, multiplied into the battle's own tempo

	UPROPERTY() TObjectPtr<UEclipseDanceTrackData> Track;
	UPROPERTY() TObjectPtr<AEclipseNpcCharacter> Opponent;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	UPROPERTY() TObjectPtr<ACameraActor> BattleCamera;
	UPROPERTY() TObjectPtr<UEclipseDanceBattleWidget> Widget;
	UPROPERTY() TObjectPtr<APointLight> StageLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> PlayerHalo;
	UPROPERTY() TObjectPtr<AActor> WaveWall;             // waveform on a translucent panel behind the opponent
	UPROPERTY() TObjectPtr<UEclipseWaveformWidget> Wave;
	TArray<TWeakObjectPtr<UUserWidget>> HiddenUi;         // HUD and prompts put away for the battle

	bool bStickLatched = false;
	int32 Unlocked = ~0;                  // EEclipseDanceStyle bits the player knows, from the game state
	int32 FirstGraded = 0;                // first bar that's scored, after the intro and the demo

	// Strafing: you and the opponent on a circle round StrafePivot, angles from where the battle began.
	FVector StrafePivot = FVector::ZeroVector;
	float StrafeCentreDeg = 0.f;
	float StrafeDeg = 0.f;                // your offset round the circle
	float OpponentDrift = 0.f;            // his, wandering slowly
	float StrafeRadius = 140.f;
	float FacingError = 0.f;              // degrees off directly-opposite
	float CamTimeDrift = 0.f;

	// HEAT mode: HEAT maxed out; everything's hotter until HeatModeEnds (song time).
	bool bHeatMode = false;
	float HeatModeEnds = 0.f;

	float OpponentStance = 150.f;         // his STANCE bar; hits knock it down, zero ends the battle

	// Deck feel: SpinT runs the vinyl spin-up and tempo; WinT the wind-down after a win; FailT the brake-and-stutter after a loss (<0 = idle).
	float SpinT = -1.f;
	float WinT = -1.f;
	float FailT = -1.f;
	int32 FailStep = -1;
	float FailLoopAt = 0.f;

	TArray<EEclipseDanceStyle> Schedule;   // Count = intro bar, nobody dances yet
	TArray<FString> IntroLines;
	FString DemoLine;                                          // "Follow my style." before the demo bars
	TArray<FString> LeadLines[(int32)EEclipseDanceStyle::Count];   // the opponent's call-outs into each style
	TArray<FString> ShowLines[(int32)EEclipseDanceStyle::Count];   // what he says while demoing each style
	int32 OpponentLevel[(int32)EEclipseDanceStyle::Count] = {};

	// Song position: advanced by the playback speed each frame, nudged by the audio thread's reports. Everything beat-related derives from it.
	float SongClock = 0.f;
	bool bWarnedPlayback = false;
	float Pitch = 1.f;                    // current playback speed
	int32 BeatIndex = -1;                 // last beat fired, counted from the segment start
	int32 LastBar = -1;
	double SwitchTime = 0.0;              // song time of the most recent switch downbeat

	EEclipseDanceStyle PlayerStyle = EEclipseDanceStyle::Count;
	EEclipseDanceStyle OpponentStyle = EEclipseDanceStyle::Count;
	double StyleChosenAt = 0.0;
	int32 Score = 0;
	int32 OpponentScore = 0;
	int32 GradedSwitches = 0;
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
	float CamFOV = 70.f;
	float CamFOVTarget = 70.f;            // steps in on GOOD/PERFECT, out on TOO EARLY/MISS
	float Wonk = 0.f;                     // misses knock the camera about; decays back to steady
	float CamTime = 0.f;

	float HaloFlash = 0.f;                // 1 on a style change, fades to the steady dancing glow
	float BeatFlash = 0.f;
	float Backbeat = 0.f;                 // swells into beats 2 and 4: drives the lights
	float SwitchPump = 0.f;               // 1 on a style switch: the wave heaves, then settles

	TMap<TWeakObjectPtr<ACharacter>, FTransform> MeshBase;   // restored on Stop
};
