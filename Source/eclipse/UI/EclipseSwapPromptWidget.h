// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseSwapPromptWidget.generated.h"

class UButton;
class UTextBlock;
class UHorizontalBox;
class UVerticalBox;
class AEclipseItemActor;

/**
 * Swap prompt — the only way to give something up.
 *
 * There is no DROP button anywhere in the game. When you reach for a pickup
 * and your hands and pockets are already full, this opens instead of the
 * pickup silently failing (or, worse, silently dropping whatever you were
 * holding, which is what the carry model used to do).
 *
 * Two boxes and an arrow: what you're carrying on the left, what you're
 * reaching for on the right. Click the RIGHT box to take the new thing (the
 * old one lands at your feet); click the LEFT box to keep what you've got.
 * No title, no cancel button — the two boxes are the whole interface, and
 * either one is a valid answer.
 *
 * Deliberately does NOT pause. The club keeps moving while you decide.
 *
 * Only one outgoing item is offered — the first swap candidate, hands before
 * pockets (see UEclipseGameStateSubsystem::GetSwapCandidates). Two boxes
 * means one choice; if you want to give up a specific pocket item instead,
 * that's the inventory panel's job.
 *
 * Designer-editable via /Game/Justin/UI/WBP_SwapPrompt (see
 * UEclipseUiBuilder::PopulateSwapPromptWBP). The boxes are filled at runtime
 * because their contents depend on what you're carrying.
 */
UCLASS()
class ECLIPSE_API UEclipseSwapPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Returns nullptr when there's nothing to trade (an empty candidate list
	// means the pickup failed for some other reason) — callers should treat
	// that as "pickup refused" and leave the world item alone.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseSwapPromptWidget* OpenForPickup(class APlayerController* PC, AEclipseItemActor* Pickup);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	// Closes the prompt if the player walks out of the pickup's radius —
	// the offer is about a specific item at a specific spot, so it
	// shouldn't follow you across the room.
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox> CandidateRow;

private:
	void BuildFallbackTree();

	// Fills CandidateRow with [old] -> [new]. Called from NativeConstruct,
	// after either the WBP or the fallback tree has supplied the row.
	void BuildCandidateBoxes();

	// Left box: keep what you're carrying, leave the pickup where it is.
	UFUNCTION() void OnKeepOldClicked();

	// Right box: take the pickup, the old item drops at your feet.
	UFUNCTION() void OnTakeNewClicked();

	// The item that would be given up. None = nothing to trade.
	FName OutgoingId;

	UPROPERTY() TObjectPtr<UButton> OldBox;
	UPROPERTY() TObjectPtr<UButton> NewBox;

	// The pickup this prompt is about. Weak: the actor lives in the world
	// and a level transition can take it out from under an open prompt.
	TWeakObjectPtr<AEclipseItemActor> PendingPickup;

	bool bDismissed = false;
};
