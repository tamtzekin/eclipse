// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EclipseBaseRoom.generated.h"

/**
 * Light scene-anchor actor placed once per room level. Holds metadata that
 * subsystems can query when the player enters this room — e.g., music cue,
 * lighting preset name, weather hooks, debug teleport label.
 *
 * This is NOT a procedural spawner. The actual geometry lives in the level —
 * walls, fixtures, NPCs, items are placed (by hand or by GenOrca MCP) directly
 * in `L_Bathroom.umap` etc.
 */
UCLASS()
class ECLIPSE_API AEclipseBaseRoom : public AActor
{
	GENERATED_BODY()

public:
	AEclipseBaseRoom();

	// Stable identifier for save/load + debug teleport ("Bathroom", "Bar", etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room")
	FName RoomKey = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room")
	FText DisplayName;

	// Music cue for this room (set in BP child / level instance).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Audio")
	TSoftObjectPtr<class USoundBase> MusicCue;

	// Whether this room blocks NPC talk-while-frozen (heat=0). Default true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Behavior")
	bool bAllowTalkWhenFrozen = false;
};
