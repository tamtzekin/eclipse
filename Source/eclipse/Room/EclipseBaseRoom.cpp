// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseBaseRoom.h"
#include "Eclipse.h"
#include "Sound/SoundBase.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

AEclipseBaseRoom::AEclipseBaseRoom()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AEclipseBaseRoom::BeginPlay()
{
	Super::BeginPlay();

	ApplyCollisionStrips();
	if (StripCollisionMeshes.Num() > 0)
	{
		LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(
			this, &AEclipseBaseRoom::OnLevelAdded);
	}

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

void AEclipseBaseRoom::ApplyCollisionStrips()
{
	if (StripCollisionMeshes.Num() == 0) return;

	UWorld* World = GetWorld();
	if (!World) return;

	// Resolve to raw pointers once; the soft pointers are only there so the
	// list doesn't drag these meshes in at editor boot.
	TSet<const UStaticMesh*> Targets;
	for (const TSoftObjectPtr<UStaticMesh>& Soft : StripCollisionMeshes)
	{
		if (const UStaticMesh* M = Soft.LoadSynchronous()) Targets.Add(M);
	}
	if (Targets.Num() == 0) return;

	int32 Stripped = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UStaticMeshComponent*> Comps;
		It->GetComponents(Comps);
		for (UStaticMeshComponent* C : Comps)
		{
			if (!C || !Targets.Contains(C->GetStaticMesh())) continue;
			C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			++Stripped;
		}
	}
	UE_LOG(LogEclipse, Log, TEXT("Room '%s': stripped collision from %d component(s) across %d mesh(es)"),
		*RoomKey.ToString(), Stripped, Targets.Num());
}

void AEclipseBaseRoom::OnLevelAdded(ULevel* /*Level*/, UWorld* World)
{
	if (World == GetWorld()) ApplyCollisionStrips();
}

void AEclipseBaseRoom::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (LevelAddedHandle.IsValid())
	{
		FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
		LevelAddedHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}
