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
};
