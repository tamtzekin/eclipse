// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseAudioSubsystem.h"
#include "Eclipse.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

void UEclipseAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("AudioSubsystem::Initialize"));
}

void UEclipseAudioSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("AudioSubsystem::Deinitialize"));
	if (CurrentMusic)
	{
		CurrentMusic->Stop();
		CurrentMusic = nullptr;
	}
	Super::Deinitialize();
}

void UEclipseAudioSubsystem::PlayUI(USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier)
{
	if (!Sound) return;
	UWorld* World = GetWorld();
	if (!World) return;
	UGameplayStatics::PlaySound2D(World, Sound, VolumeMultiplier, PitchMultiplier);
}

void UEclipseAudioSubsystem::PlaySFXAt(USoundBase* Sound, FVector Location, float VolumeMultiplier, float PitchMultiplier)
{
	if (!Sound) return;
	UWorld* World = GetWorld();
	if (!World) return;
	UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, VolumeMultiplier, PitchMultiplier);
}

void UEclipseAudioSubsystem::PlayCue(EEclipseUiCue Cue, float VolumeMultiplier)
{
	// Placeholder square-wave bleeps generated into /Game/Justin/Audio/UI —
	// deliberately cheap and obviously temporary. The FMOD plugins are
	// enabled but the project ships no banks yet, so there is nothing for
	// FMOD to play; when a Studio project exists this function is the one
	// place that has to change.
	if (!bPlaceholderUiCues) return;

	const TCHAR* Asset = nullptr;
	switch (Cue)
	{
	case EEclipseUiCue::Pickup:       Asset = TEXT("S_UI_Pickup");       break;
	case EEclipseUiCue::Use:          Asset = TEXT("S_UI_Use");          break;
	case EEclipseUiCue::Drop:         Asset = TEXT("S_UI_Drop");         break;
	case EEclipseUiCue::Equip:        Asset = TEXT("S_UI_Equip");        break;
	case EEclipseUiCue::MenuOpen:     Asset = TEXT("S_UI_MenuOpen");     break;
	case EEclipseUiCue::MenuClose:    Asset = TEXT("S_UI_MenuClose");    break;
	case EEclipseUiCue::DialogueLine: Asset = TEXT("S_UI_DialogueLine"); break;
	case EEclipseUiCue::MeterUp:      Asset = TEXT("S_UI_MeterUp");      break;
	case EEclipseUiCue::MeterDown:    Asset = TEXT("S_UI_MeterDown");    break;
	}
	if (!Asset) return;

	// Cached per cue: these fire on every pickup and every dialogue line, so
	// a synchronous load each time would hitch the frame it lands on.
	TWeakObjectPtr<USoundBase>& Slot = CueCache.FindOrAdd(Cue);
	if (!Slot.IsValid())
	{
		Slot = LoadObject<USoundBase>(nullptr,
			*FString::Printf(TEXT("/Game/Justin/Audio/UI/%s.%s"), Asset, Asset));
	}
	if (USoundBase* Sound = Slot.Get())
	{
		PlayUI(Sound, VolumeMultiplier);
	}
}

void UEclipseAudioSubsystem::PlayMusic(USoundBase* Sound, float FadeInSeconds, float StartTimeSeconds)
{
	if (!Sound) return;
	UWorld* World = GetWorld();
	if (!World) return;

	// Fade out the previous track in parallel — the new track fades in fresh.
	if (CurrentMusic)
	{
		CurrentMusic->FadeOut(FMath::Max(FadeInSeconds * 0.5f, 0.25f), 0.f);
		CurrentMusic = nullptr;
	}

	// SpawnSound2D returns a UAudioComponent we can keep + fade.
	CurrentMusic = UGameplayStatics::SpawnSound2D(
		World, Sound,
		/*VolumeMultiplier=*/0.f,    // start silent, FadeIn to 1
		/*PitchMultiplier=*/1.f,
		StartTimeSeconds,           // skip an intro that doesn't work as a loop-in
		/*ConcurrencyOverride=*/nullptr,
		/*bPersistAcrossLevelTransition=*/true,
		/*bAutoDestroy=*/true);

	if (CurrentMusic)
	{
		// Fade up to the global MusicVolume multiplier — 0 keeps the track
		// silent (slice currently ships muted; flip via SetMusicVolume later).
		CurrentMusic->FadeIn(FadeInSeconds, /*FadeVolumeLevel=*/MusicVolume);
		UE_LOG(LogEclipse, Log, TEXT("Audio: PlayMusic '%s' fade-in %.1fs vol=%.2f start=%.1fs"),
			*Sound->GetName(), FadeInSeconds, MusicVolume, StartTimeSeconds);
	}
}

void UEclipseAudioSubsystem::SetMusicVolume(float Volume)
{
	MusicVolume = FMath::Clamp(Volume, 0.f, 1.f);
	// Adjust live track immediately if one is playing.
	if (CurrentMusic)
	{
		CurrentMusic->AdjustVolume(/*Time=*/0.5f, MusicVolume);
	}
	UE_LOG(LogEclipse, Log, TEXT("Audio: SetMusicVolume %.2f"), MusicVolume);
}

void UEclipseAudioSubsystem::StopMusic(float FadeOutSeconds)
{
	if (!CurrentMusic) return;
	CurrentMusic->FadeOut(FadeOutSeconds, /*FadeVolumeLevel=*/0.f);
	CurrentMusic = nullptr;
	UE_LOG(LogEclipse, Log, TEXT("Audio: StopMusic fade-out %.1fs"), FadeOutSeconds);
}

UAudioComponent* UEclipseAudioSubsystem::PlaySliced(USoundBase* Sound,
	float StartTime, float Duration, float PitchMultiplier, float VolumeMultiplier,
	float FadeOutSeconds)
{
	if (!Sound) return nullptr;
	UWorld* World = GetWorld();
	if (!World) return nullptr;

	// SpawnSound2D returns a transient UAudioComponent that bAutoDestroy will
	// clean up once playback ends. We hand that lifetime over and just stop
	// the source after Duration via the world timer manager.
	UAudioComponent* C = UGameplayStatics::SpawnSound2D(
		World, Sound,
		VolumeMultiplier,
		PitchMultiplier,
		FMath::Max(0.f, StartTime),
		/*ConcurrencyOverride=*/nullptr,
		/*bPersistAcrossLevelTransition=*/false,
		/*bAutoDestroy=*/true);

	if (C && Duration > 0.f)
	{
		// Stop with a fade so consecutive slices crossfade into each other —
		// short fade (~0.04s) gives a chopped texture, longer (~0.18s) reads
		// as a continuous vocal melody. We capture a weak handle so we don't
		// keep the component alive past auto-destroy.
		TWeakObjectPtr<UAudioComponent> Weak = C;
		const float Fade = FMath::Max(0.01f, FadeOutSeconds);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle,
			FTimerDelegate::CreateLambda([Weak, Fade]()
			{
				if (UAudioComponent* Live = Weak.Get())
				{
					Live->FadeOut(Fade, /*FadeVolumeLevel=*/0.f);
				}
			}),
			Duration, /*bLoop=*/false);
	}
	return C;
}
