// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameInstance.h"
#include "Eclipse.h"
#include "Subsystems/EclipseGameStateSubsystem.h"

void UEclipseGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogEclipse, Log, TEXT("EclipseGameInstance::Init — booting subsystems"));

	// Touch the GameStateSubsystem so it's instantiated before we ask it to load,
	// then attempt to restore from the autosave slot. Returns false if no slot
	// exists yet (fresh install / first run), in which case the defaults stand.
	if (UEclipseGameStateSubsystem* GS = GetSubsystem<UEclipseGameStateSubsystem>())
	{
		GS->TryLoadCurrent();
	}
}

void UEclipseGameInstance::Shutdown()
{
	UE_LOG(LogEclipse, Log, TEXT("EclipseGameInstance::Shutdown — autosave"));

	if (UEclipseGameStateSubsystem* GS = GetSubsystem<UEclipseGameStateSubsystem>())
	{
		GS->SaveCurrent();
	}

	Super::Shutdown();
}
