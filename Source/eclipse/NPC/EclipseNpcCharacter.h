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

	// ── Behavior flags ──
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bTalkable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bIsKeyNPC = false; // Bartenders, Cloakroom Lady — always talkable even at thirst=0.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bIsHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	bool bStationary = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|NPC")
	float TalkRadius = 200.f; // 2 m default; 320 for key NPCs.

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
};
