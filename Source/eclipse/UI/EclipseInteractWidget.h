// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseInteractWidget.generated.h"

class UTextBlock;
class AEclipseNpcCharacter;

/**
 * Screen-space interact prompt. Shown when the player is close enough to an
 * NPC or item to interact. Binds to EclipseInteractSubsystem delegates.
 *
 * Mirrors the HTML #interact-3d element:
 *   color: #51eefc  font: BMSPA  letter-spacing: 2px  glow text-shadow
 *
 * Blueprint child must contain a UTextBlock named exactly "PromptText".
 */
UCLASS()
class ECLIPSE_API UEclipseInteractWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// Optional bind: built programmatically in Initialize() if WBP omits it.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText;

private:
	UFUNCTION()
	void HandleNearTalkableChanged(AEclipseNpcCharacter* Npc);

	UFUNCTION()
	void HandleNearItemChanged(AActor* Item);

	UFUNCTION()
	void HandleDialogueOpened(AEclipseNpcCharacter* Npc);

	UFUNCTION()
	void HandleDialogueClosed();

	// Track both so we can pick the right display (talkable wins)
	TWeakObjectPtr<AEclipseNpcCharacter> CachedNpc;
	TWeakObjectPtr<AActor>               CachedItem;

	// Prompt stays hidden for the duration of an open dialogue, regardless
	// of NearTalkable/NearItem changes underneath it.
	bool bDialogueOpen = false;

	void RefreshPrompt();

	// ── World-anchored name label ──────────────────────────────────────
	// The prompt is no longer parked at a fixed spot on screen: it tracks
	// the projected position of whatever it's naming, offset onto the
	// player's side of it, so the label reads as belonging to that object
	// and swings around it as the player circles.
	void TickPromptPosition();

	// Gap between the subject's silhouette and the label, and how far above
	// the subject's top the label floats.
	static constexpr float LabelStandoffCm = 18.f;
	static constexpr float LabelLiftCm     = 14.f;

	// Anchors/alignment only need setting once; the per-frame work is the
	// position alone.
	bool bPromptSlotReady = false;

	// ── Pickup card ────────────────────────────────────────────────────
	// A framed picture of what you just collected, shown beside the
	// interact prompt and faded out. Confirms WHAT was taken without
	// making the player open the inventory to check.
	UFUNCTION()
	void HandleItemPickedUp(FName ItemId);

	virtual void NativeTick(const FGeometry& G, float DeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<class UBorder> PickupCard;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<class UImage>  PickupImage;

	// Eight white copies of the icon, nudged one step in each direction and
	// stacked behind the real one. A UBorder can only ever outline its own
	// rectangle, which is why the card used to read as a white square around
	// a picture; drawing the sprite itself is what makes the outline follow
	// the silhouette.
	UPROPERTY() TArray<TObjectPtr<class UImage>> PickupOutline;

	void EnsurePickupCard();

	float PickupCardTimer = 0.f;
	static constexpr float PickupCardHoldSeconds = 1.8f;
	static constexpr float PickupCardFadeSeconds = 0.9f;
	// Stroke width, in pixels, of the sprite outline.
	static constexpr float PickupOutlinePx = 2.f;
};
