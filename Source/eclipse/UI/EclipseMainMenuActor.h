// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EclipseMainMenuActor.generated.h"

/**
 * Tiny world-anchor actor placed once in L_MainMenu. On BeginPlay, creates and
 * shows the WBP_MainMenu widget over the local player's viewport — pure UI
 * mode, cursor on. No level geometry; this is the entire "main menu level".
 */
UCLASS()
class ECLIPSE_API AEclipseMainMenuActor : public AActor
{
	GENERATED_BODY()

public:
	AEclipseMainMenuActor();

protected:
	virtual void BeginPlay() override;
};
