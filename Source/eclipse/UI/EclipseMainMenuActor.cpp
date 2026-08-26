// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseMainMenuActor.h"
#include "Eclipse.h"
#include "EclipseMainMenuWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Subsystems/EclipseDemoFlow.h"
#include "Subsystems/EclipseDemoSettings.h"
#include "Engine/GameInstance.h"

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
	// Demo flow decides whether the start screen appears at all. In PIE it
	// normally doesn't, so pressing Play on L_MainMenu drops straight into
	// the first playable level instead of making you click NEW GAME every
	// single iteration. Packaged builds always get the menu.
	// See UEclipseDemoSettings (Project Settings -> Game -> Eclipse Demo).
	UEclipseDemoFlow* Flow = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseDemoFlow>() : nullptr;
	if (Flow && !Flow->ShouldShowStartScreen())
	{
		const FName Skip = UEclipseDemoSettings::Get().FirstPlayableLevel;
		UE_LOG(LogEclipse, Log, TEXT("MainMenuActor: start screen skipped (PIE) -> %s"),
			*Skip.ToString());
		UGameplayStatics::OpenLevel(W, Skip);
		return;
	}

	UEclipseMainMenuWidget::OpenForPlayer(PC);
	UE_LOG(LogEclipse, Log, TEXT("MainMenuActor: opened menu for %s"), *PC->GetName());
}
