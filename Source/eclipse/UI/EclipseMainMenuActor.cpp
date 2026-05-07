// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseMainMenuActor.h"
#include "Eclipse.h"
#include "EclipseMainMenuWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

AEclipseMainMenuActor::AEclipseMainMenuActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AEclipseMainMenuActor::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogEclipse, Warning, TEXT("MainMenuActor: no PlayerController — menu not shown"));
		return;
	}
	UEclipseMainMenuWidget::OpenForPlayer(PC);
	UE_LOG(LogEclipse, Log, TEXT("MainMenuActor: opened menu for %s"), *PC->GetName());
}
