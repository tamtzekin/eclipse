// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseNpcCharacter.h"
#include "Eclipse.h"
#include "GameFramework/CharacterMovementComponent.h"

AEclipseNpcCharacter::AEclipseNpcCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AEclipseNpcCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (bStationary)
	{
		// Disable AI tick / movement input — set dressing characters don't wander.
		GetCharacterMovement()->DisableMovement();
	}
	UE_LOG(LogEclipse, Verbose, TEXT("NPC '%s' ready (talkable=%d key=%d radius=%.0f)"),
		*NpcName.ToString(), bTalkable, bIsKeyNPC, TalkRadius);
}
