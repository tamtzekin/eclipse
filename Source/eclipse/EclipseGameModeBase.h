// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "EclipseGameModeBase.generated.h"

/**
 * Base game mode for ECLIPSE. Hand-authored levels point at this (or a Blueprint
 * child of it) so we can override default pawn / HUD / player controller per level
 * later if needed. For the bathroom slice, the defaults are sufficient.
 */
UCLASS()
class ECLIPSE_API AEclipseGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEclipseGameModeBase();
};
