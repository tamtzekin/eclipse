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

void UEclipseAudioSubsystem::PlayMusic(USoundBase* Sound, float FadeInSeconds)
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
		/*StartTime=*/0.f,
		/*ConcurrencyOverride=*/nullptr,
		/*bPersistAcrossLevelTransition=*/true,
		/*bAutoDestroy=*/true);

	if (CurrentMusic)
	{
		CurrentMusic->FadeIn(FadeInSeconds, /*FadeVolumeLevel=*/1.f);
		UE_LOG(LogEclipse, Log, TEXT("Audio: PlayMusic '%s' fade-in %.1fs"),
			*Sound->GetName(), FadeInSeconds);
	}
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
