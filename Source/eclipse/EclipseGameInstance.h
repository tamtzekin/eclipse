// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "EclipseGameInstance.generated.h"

/**
 * GameInstance — single instance per game session. Subsystems live here:
 *  - UEclipseGameStateSubsystem (heat / thirst / inventory / quest / chapter)
 *  - UEclipseDialogueSubsystem  (Articy runtime wrapper)
 *  - UEclipseInteractSubsystem  (lives on World, but referenced via this)
 *
 * On `Init` we boot subsystems and load any existing SaveGame slot.
 */
UCLASS()
class ECLIPSE_API UEclipseGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;
};
