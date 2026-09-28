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

	// First-time lessons (strafe, then copying his moveset) run until mastered, then the scored battle; later
	// battles demo only what the track introduces. The schedule is written a few bars ahead as the song plays.
	enum class EPhase : uint8 { Intro, Strafe, Learn, Battle };
	void BuildSchedule();
	void AppendSchedule();
	void Attack(bool bHeavy);          // your answer: hands (light) or legs (heavy)
	void StartMoveset(int32 Bar);      // his next pattern: he calls it, you answer (the lesson) or you answer live (the battle)
	void TickNotes(float Song);        // retires notes you let pass
	void AddPlayerStance(float Amount);   // wrong answers cost you balance
	void CheckBalance();                  // drift + those costs; full either way and you go down
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
	EPhase Phase = EPhase::Intro;
	bool bTutorial = true;
	FRandomStream Rng;
	TArray<EEclipseDanceStyle> Pool;      // styles this battle uses: known plus what the track introduces
	int32 MaxBars = 0;                    // the track runs out here
	int32 StrafeStart = -1, DemoStart = 0, DemoEnd = 0, BattleStart = 0;   // phase boundaries, in bars
	float FacedSeconds = 0.f;             // strafe lesson: unbroken time facing him
	int32 PatternHits = 0, PatternNotes = 0;   // how much of his current pattern you answered
	int32 LearnPasses = 0;                     // patterns copied cleanly during the lesson
	int32 MovesetStart = MAX_int32;            // first bar he throws a pattern on
	TArray<FString> StrafeLines;
	TArray<FString> WrongHeavyLines, WrongLightLines;   // what he says when you answer with the wrong limb
	bool bBeatenBefore = false;                         // he's lost to you before: no lesson, different mouth
	bool bPendingDefeatTalk = false;                    // he has something to say once you've beaten him
	FString BothLine;

	// Strafing: you and the opponent on a circle round StrafePivot, angles from where the battle began.
	FVector StrafePivot = FVector::ZeroVector;
	float StrafeCentreDeg = 0.f;
	float StrafeDeg = 0.f;                // your offset round the circle
	float OpponentDrift = 0.f;            // his, wandering slowly
	float StrafeRadius = 85.f;
	float FacingError = 0.f;              // degrees off directly-opposite
	float CamTimeDrift = 0.f;
	float DriftSpeed = 0.f, LastDrift = 0.f;               // how fast he's sliding round the circle, for the lean

	// His moveset: notes at song times you have to answer with the same weight.
	struct FPendingNote
	{
		float Time = 0.f;
		bool bHeavy = false;
		bool bHis = false;      // he dances it: pose, camera, marker
		bool bAnswer = false;   // you have to press it
		bool bDone = false;
		bool bCued = false;     // the camera has braced for it
		bool bStruck = false;   // his pose has played
		bool bHit = false;      // you answered it
		bool bWithHim = false;  // answered as he danced it (perfect), not on the reply bar (good)
	};
	TArray<FPendingNote> Notes;
	TArray<FString> Movesets;          // patterns from his Ink <id>_moveset knot
	int32 MovesetIndex = 0;
	int32 NotesBar = -1;               // bar his current pattern started on

	// Your own stance, Sekiro-style: wrong answers fill it, a full bar leaves you flat-footed for a moment.
	float PlayerStance = 0.f;
	float StaggeredUntil = -1.f;          // song time you can move again
	// His tell: a slow rise (light lead) or sink (heavy lead) over the two beats before he steps.
	float TelegraphFrom = -1.f, TelegraphTo = -1.f;   // song times
	float TelegraphDir = 0.f;                          // +1 up, -1 down
	int32 PatternPerfects = 0;                         // perfect answers inside the current pattern

	// HEAT mode: a run of clean answers lights it, and everything burns hotter until HeatModeEnds (song time).
	int32 Streak = 0;
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
	int32 OpponentLevel[(int32)EEclipseDanceStyle::Count] = {};

	// Song position: advanced by the playback speed each frame, nudged by the audio thread's reports. Everything beat-related derives from it.
	float SongClock = 0.f;
	bool bWarnedPlayback = false;
	float Pitch = 1.f;                    // current playback speed
	int32 BeatIndex = -1;                 // last beat fired, counted from the segment start
	int32 LastBar = -1;

	EEclipseDanceStyle PlayerStyle = EEclipseDanceStyle::Count;
	EEclipseDanceStyle OpponentStyle = EEclipseDanceStyle::Count;
	int32 Score = 0;
	int32 OpponentScore = 0;
	int32 Counts[(int32)EGrade::Count] = {};
	bool bOpponentHit = false;
	float Performance = 0.5f;             // running 0..1 grade average; drives how hard the camera punches in
	bool bFinished = false;
	float EndWait = -1.f;                 // counts the wind-down after the last move, then closes the battle

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

	// Attack poses, 1 at the strike and easing off: X light (head dips), Y heavy (whole body from the feet).
	FVector2D PlayerTilt = FVector2D::ZeroVector;
	FVector2D OpponentTilt = FVector2D::ZeroVector;

	float HaloFlash = 0.f;                // 1 on a style change, fades to the steady dancing glow
	float BeatFlash = 0.f;
	float Backbeat = 0.f;                 // swells into beats 2 and 4: drives the lights
	float SwitchPump = 0.f;               // 1 on a style switch: the wave heaves, then settles

	TMap<TWeakObjectPtr<ACharacter>, FTransform> MeshBase;   // restored on Stop
};
