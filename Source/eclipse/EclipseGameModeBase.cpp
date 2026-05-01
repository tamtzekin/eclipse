// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameModeBase.h"
#include "Player/EclipsePlayerCharacter.h"

AEclipseGameModeBase::AEclipseGameModeBase()
{
	DefaultPawnClass = AEclipsePlayerCharacter::StaticClass();
	// PlayerControllerClass / HUDClass left as engine defaults; override in BP child.
}
