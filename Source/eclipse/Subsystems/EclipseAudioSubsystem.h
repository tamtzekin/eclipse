// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseAudioSubsystem.generated.h"

class USoundBase;
class UAudioComponent;

/**
 * Single source of truth for non-trivial audio routing.
 *
 *   PlayUI / PlaySFXAt — thin wrappers around UGameplayStatics for
 *     fire-and-forget one-shots (UI clicks, item pickups, dialogue beats).
 *
 *   PlayMusic / StopMusic — persistent UAudioComponent with crossfade so
 *     room/chapter transitions don't pop. Mirrors the JS prototype's Howler.js
 *     `bgMusic.fade(...)` pattern.
 *
 * Sound assets aren't shipped with the slice yet — call sites declare USoundBase
 * UPROPERTYs that designers wire up in BP defaults. PlayUI / PlaySFXAt no-op
 * when given a null sound, so the wiring is safe even before assets land.
 */
/** Named UI moments — see UEclipseAudioSubsystem::PlayCue. */
UENUM(BlueprintType)
enum class EEclipseUiCue : uint8
{
	Pickup       UMETA(DisplayName = "Pick up item"),
	Use          UMETA(DisplayName = "Use item"),
	Drop         UMETA(DisplayName = "Drop item"),
	Equip        UMETA(DisplayName = "Equip / place item"),
	MenuOpen     UMETA(DisplayName = "Open menu"),
	MenuClose    UMETA(DisplayName = "Close menu"),
	DialogueLine UMETA(DisplayName = "New dialogue line"),
	MeterUp      UMETA(DisplayName = "Heat/Thirst gained"),
	MeterDown    UMETA(DisplayName = "Heat/Thirst lost"),
};

UCLASS()
class ECLIPSE_API UEclipseAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── One-shots ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlayUI(USoundBase* Sound, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f);

	/**
	 * The interface layer's vocabulary, named rather than passed around as
	 * asset pointers. Every call site says WHAT happened, not which wave to
	 * play, so the whole set can be re-pointed at FMOD events later by
	 * changing PlayCue alone.
	 */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlayCue(EEclipseUiCue Cue, float VolumeMultiplier = 1.f);

	// The S_UI_* bleeps are synthesised placeholders. Off while real foley is
	// being added a piece at a time — flip back on (or delete this once every
	// cue has a real asset) rather than tearing out the call sites, which are
	// already in the right places.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Audio")
	bool bPlaceholderUiCues = false;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlaySFXAt(USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f);

	// ── Music (persistent track with crossfade) ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlayMusic(USoundBase* Sound, float FadeInSeconds = 1.f, float StartTimeSeconds = 0.f);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void StopMusic(float FadeOutSeconds = 1.f);

	// Global music multiplier — 0.0 mutes all music tracks, 1.0 plays at the
	// asset's authored level. Lives separately from PlayMusic's per-call
	// VolumeMultiplier so we can ship the slice with music wired-up but
	// silent (default = 0) and unmute later from one call site.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void SetMusicVolume(float Volume);

private:
	// Resolved lazily by PlayCue; weak so a level flush can reclaim them.
	TMap<EEclipseUiCue, TWeakObjectPtr<USoundBase>> CueCache;
public:

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	float GetMusicVolume() const { return MusicVolume; }

	// ── Sliced one-shot ──
	// Plays a small chunk of a sound starting at StartTime, optionally stopping
	// after Duration seconds with a tiny fade-out so the cut isn't a click.
	// Used by the dialogue widget to splice short "mumble" syllables out of
	// angel_voice.ogg as each word fades in. PitchMultiplier randomizes the
	// timbre. Returns the spawned AudioComponent (caller can keep it for
	// further control, or ignore — bAutoDestroy is on).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	UAudioComponent* PlaySliced(USoundBase* Sound,
	                            float StartTime = 0.f,
	                            float Duration  = 0.f,
	                            float PitchMultiplier  = 1.f,
	                            float VolumeMultiplier = 1.f,
	                            float FadeOutSeconds   = 0.04f);

private:
	UPROPERTY()
	TObjectPtr<UAudioComponent> CurrentMusic;

	// Was 0 while the slice shipped deliberately muted. Room music now comes
	// through PlayMusic (2D, from the room's BeginPlay) rather than an
	// AmbientSound in the level, so muting this mutes the soundtrack.
	UPROPERTY()
	float MusicVolume = 1.0f;
};
