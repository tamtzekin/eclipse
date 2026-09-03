// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseBaseRoom.h"
#include "Eclipse.h"
#include "Sound/SoundBase.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

AEclipseBaseRoom::AEclipseBaseRoom()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AEclipseBaseRoom::BeginPlay()
{
	Super::BeginPlay();

	// Auto-play this room's music cue on enter. The AudioSubsystem handles
	// crossfade with whatever was playing before, and respects its global
	// MusicVolume — so even though this fires on every level load, the slice
	// ships muted (MusicVolume=0) until the mix is dialled in.
	if (MusicCue.IsNull()) return;

	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	UEclipseAudioSubsystem* Audio = GI ? GI->GetSubsystem<UEclipseAudioSubsystem>() : nullptr;
	if (!Audio) return;

	// Synchronous load is fine — music tracks are small enough and we're
	// already taking the level-load hit. Could be StreamableManager-backed
	// later if we ever add giant cinematic tracks.
	USoundBase* Sound = MusicCue.LoadSynchronous();
	if (!Sound)
	{
		UE_LOG(LogEclipse, Warning, TEXT("Room '%s': MusicCue failed to resolve at '%s'"),
			*RoomKey.ToString(), *MusicCue.ToString());
		return;
	}

	Audio->PlayMusic(Sound, MusicFadeInSeconds, MusicStartSeconds);
	UE_LOG(LogEclipse, Log, TEXT("Room '%s': PlayMusic '%s' fade=%.1fs start=%.1fs"),
		*RoomKey.ToString(), *Sound->GetName(), MusicFadeInSeconds, MusicStartSeconds);
}
