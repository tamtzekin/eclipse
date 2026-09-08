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

	virtual void BeginPlay() override;

	// Stable identifier for save/load + debug teleport ("Bathroom", "Bar", etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room")
	FName RoomKey = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room")
	FText DisplayName;

	// Music cue for this room (set in BP child / level instance). Auto-plays
	// on BeginPlay via UEclipseAudioSubsystem. Soft-pointer so the asset only
	// loads on level enter, not at editor boot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Audio")
	TSoftObjectPtr<class USoundBase> MusicCue;

	// Crossfade time when this room's music takes over from the previous track.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Audio")
	float MusicFadeInSeconds = 1.5f;

	// Seconds to skip at the head of MusicCue. A club track that opens on a
	// long ambient build lands flat when the room does — start it where the
	// beat already is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room")
	float MusicStartSeconds = 0.f;

	// Whether this room blocks NPC talk-while-frozen (heat=0). Default true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Behavior")
	bool bAllowTalkWhenFrozen = false;

	/**
	 * Meshes whose imported collision is unusable — a single auto-generated
	 * convex hull that swallows the shape whole and leaves a solid slab
	 * hanging over the floor the player is meant to walk on. Every component
	 * using one of these has its collision switched off at BeginPlay.
	 *
	 * By mesh rather than by actor because the offending actors live in a
	 * streaming sublevel owned by someone else (see CLAUDE.md section 0) —
	 * we can't save a per-instance override into their package, and actor
	 * names there are auto-generated and unstable. Fixing the collision on
	 * the source assets is the real repair; this keeps the level walkable
	 * until then.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Room|Collision")
	TArray<TSoftObjectPtr<class UStaticMesh>> StripCollisionMeshes;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyCollisionStrips();

	// Re-runs the strip when a streaming sublevel arrives. The offending
	// actors live in one, and whether it is already loaded when this room's
	// BeginPlay fires is not something the room gets to know.
	void OnLevelAdded(class ULevel* Level, class UWorld* World);
	FDelegateHandle LevelAddedHandle;
};
