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

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlaySFXAt(USoundBase* Sound, FVector Location, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f);

	// ── Music (persistent track with crossfade) ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void PlayMusic(USoundBase* Sound, float FadeInSeconds = 1.f);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void StopMusic(float FadeOutSeconds = 1.f);

	// Global music multiplier — 0.0 mutes all music tracks, 1.0 plays at the
	// asset's authored level. Lives separately from PlayMusic's per-call
	// VolumeMultiplier so we can ship the slice with music wired-up but
	// silent (default = 0) and unmute later from one call site.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Audio")
	void SetMusicVolume(float Volume);

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

	// Default to 0 — slice ships with music silent; designer or a debug
	// console command can SetMusicVolume(1.0) once the mix is dialled in.
	UPROPERTY()
	float MusicVolume = 0.0f;
};
