// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EclipseNpcCharacter.generated.h"

UENUM(BlueprintType)
enum class EEclipseBubbleType : uint8
{
	None       UMETA(DisplayName = "None"),
	Question   UMETA(DisplayName = "?"),
	Exclaim    UMETA(DisplayName = "!"),
	Both       UMETA(DisplayName = "?!"),
	Muted      UMETA(DisplayName = "…")
};

/**
 * NPC base — a stationary or wandering character that may be talkable.
 * Mirrors the npc data shape in index.html:
 *   { name, dialogueId, talkable, isKeyNPC, hidden, talkRadius, bubbleType,
 *     stationary, stallIdx, isAngelSeeker, isBartender, isCloakroom, ... }
 *
 * Bathroom-slice NPCs:
 *   - Girl in the Bathroom (Angel Seeker)  — talkable, isAngelSeeker
 *   - Stall Voice 1/2/4                    — talkable, stationary, stallIdx 1/2/4
 *   - Stall Guy                            — non-talkable set dressing (cyan tint)
 */
UCLASS()
class ECLIPSE_API AEclipseNpcCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AEclipseNpcCharacter();

	// ── Identity ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	FName NpcName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	FName DialogueId = NAME_None;

	// The name players actually see (dialogue caption, "[E] TALK TO ..."),
	// derived from DialogueId — which is always the snake_case of the
	// character's .ink filename (Bouncer_Outside.ink -> bouncer_outside ->
	// "BOUNCER OUTSIDE"). Deriving it means the UI follows the writing
	// rather than whatever the level actor or its skeletal mesh is called;
	// NpcName stays the internal identity key (save data, Angel lookup).
	// Falls back to NpcName for set-dressing NPCs with no dialogue.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|NPC")
	FText GetDisplayName() const;

	// ── Behavior flags ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bTalkable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bIsKeyNPC = false; // Bartenders, Cloakroom Lady — always talkable even at thirst=0.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bIsHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bStationary = true;

	// When false this NPC never turns toward the player — no approach lean,
	// no dialogue lock-on. Guarded in the NPC itself rather than at the
	// InteractSubsystem call sites so both turn paths are covered at once.
	//
	// Defaults OFF: the swivel reads as tracking rather than noticing, and
	// the rigs have no turn animation to sell it. This only governs the
	// BODY — the talk radius and its lock-on for dialogue are unaffected.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bTurnToFacePlayer = false;

	// THE LEVER for how close you must stand to talk. Per-instance: select
	// the NPC in the World Outliner and edit "Talk Radius" under Eclipse|NPC
	// in the Details panel — it's live-editable during PIE, so you can drag
	// it while walking up to someone and feel the change immediately. This
	// value is only the default for newly placed NPCs.
	// ~0.9 m — deliberately tight; anything wider and you get "caught" by
	// NPCs you were only walking past.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC",
		meta = (ClampMin = "40.0", ClampMax = "600.0"))
	float TalkRadius = 220.f;

	// Extra camera pitch, in degrees, while this NPC is being talked to.
	// Positive raises the camera and looks down — for characters the default
	// eyeline can't see cleanly (Zbigniewa is behind a wall).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	float DialogueCameraPitch = 0.f;

	// When true, skip the BeginPlay floor-snap line-trace entirely and keep
	// the actor exactly where it was placed. Use when the actor sits above
	// an isolated/disconnected piece of collision (e.g. a small set-dressing
	// pedestal) that the trace would otherwise snap onto instead of the
	// intended floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bSkipFloorSnap = false;

	// When true, the talkable check skips the line-of-sight raytrace — i.e.
	// this NPC can be talked to *through* walls / partitions. Use for the
	// audio-only stall voices (`STALL_VOICE_*`) where the player is meant
	// to converse with someone they can't see. Default false: walls block.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bIgnoreLineOfSight = false;

	// Optional: a separate set-dressing actor (e.g. a level-artist-placed
	// mesh) that this NPC's invisible interact proxy stands in for. The
	// interact subsystem's LOS trace ignores this actor too, so the NPC's
	// own visual double doesn't block its own line-of-sight check when the
	// proxy is positioned exactly where that mesh stands. Soft reference —
	// this actor is often in a different (sub-)level, and a hard TObjectPtr
	// there is an illegal cross-map reference at save time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	TSoftObjectPtr<AActor> VisualProxyActor;

	// VisualProxyActor's own mesh usually doesn't face the same way as this
	// (normally-invisible) actor's forward axis — added to this actor's yaw
	// every tick before applying it to the visual proxy, so the two can be
	// out of alignment by any amount and still be corrected. Tune live: with
	// PIE running, select this NPC in the World Outliner and drag the value
	// in the Details panel while approaching it as the player — no rebuild
	// needed. Try 180 first (most common case), then nudge from there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float VisualProxyYawOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	EEclipseBubbleType BubbleType = EEclipseBubbleType::Question;

	// ── Quest / role flags ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Roles")
	bool bIsAngelSeeker = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Roles")
	bool bIsBartender = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Roles")
	bool bIsCloakroom = false;

	// Stall metadata (for stall voice NPCs)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Stall")
	int32 StallIndex = -1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Stall")
	bool bStallErratic = false;

	// ── Visual tint (for Stall Guy & generic NPCs) ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Visuals")
	float HueDegrees = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Visuals")
	float Lightness = 40.f;

	// ── Dialogue portrait — shown stuck to the left of the dialogue panel ──
	// Drag a Texture2D from /Game/Justin/Characters/Portraits onto this in the
	// level instance to give the NPC a face during conversations.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Portrait")
	TObjectPtr<class UTexture2D> PortraitTexture;

	// ── Per-NPC "mumble" voice — sliced into syllables as each dialogue word
	//    fades in. Null = silent (most NPCs). Only the Angel ships with a
	//    mumble track today (/Game/Audio/angel_voice). Designer-assignable so
	//    individual NPCs can have their own voice character later.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Audio")
	TObjectPtr<class USoundBase> MumbleSound;

	// ── Step-aside: NPC slides out of the way once the player triggers a quest
	//    beat (e.g. AngelSeeker after "Open the stall and enter"). The offset
	//    is added to her spawn location captured at BeginPlay; tune in the
	//    level per-NPC so she clears the doorway cleanly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Quest")
	FVector StepAsideOffset = FVector(0.f, 200.f, 0.f);   // default: 2m to the right

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC|Quest")
	float StepAsideSpeed = 0.7f;   // alpha/sec — 0.7 ≈ ~1.4s to fully step aside

	UFUNCTION(BlueprintCallable, Category = "Eclipse|NPC|Quest")
	void StepAside();

	// ── Face-player: turns to face whoever's talking to them on dialogue
	//    open, turns back to their original facing on close. Called by
	//    EclipseDialogueSubsystem (mirrors the player's own StartFaceTarget).
	//    Tracks the target actor live (not a one-time position snapshot) so
	//    the NPC keeps facing them even if they walk/strafe afterward.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|NPC")
	void StartFacePlayer(AActor* PlayerActor);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|NPC")
	void StopFacePlayer();

	// ── Approach turn: gradual pre-lock lean toward the player as they close
	//    in from the wide outer radius, well before the tight lock-on radius
	//    fires StartFacePlayer. Alpha 0 = original facing, 1 = fully facing
	//    PlayerActor. Called every tick by EclipseInteractSubsystem with the
	//    live proximity alpha; overridden by StartFacePlayer's full lock
	//    whenever that's active.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|NPC")
	void UpdateApproachTurn(float Alpha, AActor* PlayerActor);

	// ── Speech bubble (?, !, …) — floats above the NPC head in 3D space ──
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<class UWidgetComponent> BubbleWidget;

	// Override the bubble widget class in BP if you want a custom layout
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|UI")
	TSubclassOf<class UEclipseSpeechBubbleWidget> BubbleWidgetClass;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void RefreshBubble(bool bMuted = false);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

private:
	UFUNCTION()
	void HandleHighlightToggled(bool bActive);

	// Captured at BeginPlay so StepAside() lerps from the original spawn pose.
	FVector OriginalLocation = FVector::ZeroVector;
	bool    bSteppingAside   = false;
	float   StepAsideAlpha   = 0.f;

	// Face-player state — see StartFacePlayer/StopFacePlayer.
	FRotator OriginalFacingRotation = FRotator::ZeroRotator;   // captured post floor-snap in BeginPlay
	bool     bFacingPlayer          = false;
	TWeakObjectPtr<AActor> FacePlayerTarget;

	// Approach-turn state — see UpdateApproachTurn. Ignored while bFacingPlayer.
	float ApproachTurnAlpha = 0.f;
	TWeakObjectPtr<AActor> ApproachTurnTarget;
};
