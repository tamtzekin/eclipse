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
};
