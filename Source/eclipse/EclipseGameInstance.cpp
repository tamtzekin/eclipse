// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameInstance.h"
#include "Eclipse.h"

void UEclipseGameInstance::Init()
{
	Super::Init();
	UE_LOG(LogEclipse, Log, TEXT("EclipseGameInstance::Init — booting subsystems"));
	// TODO(slice): trigger SaveGame load on the GameStateSubsystem if a slot exists.
	// Subsystems auto-instantiate on first GetSubsystem() call; we just need to
	// reach for them once during boot to ensure construction happens early.
}

void UEclipseGameInstance::Shutdown()
{
	UE_LOG(LogEclipse, Log, TEXT("EclipseGameInstance::Shutdown — autosave"));
	// TODO(slice): autosave via UEclipseGameStateSubsystem on quit.
	Super::Shutdown();
}
