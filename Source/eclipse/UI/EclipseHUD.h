// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "EclipseHUD.generated.h"

class UEclipseInteractWidget;
class UEclipseDialogueWidget;
class UEclipseHUDWidget;
class UEclipseVnPortraitsWidget;
class UEclipseChapterCardWidget;

/**
 * Eclipse HUD — creates and owns all screen-space UMG widgets for the slice.
 *
 * Set this as the HUD class on BP_EclipseGameMode. The three
 * TSubclassOf properties point at the Blueprint children of each C++ widget.
 *
 * Widget hierarchy (z-order by AddToViewport ZOrder param):
 *   0  WBP_HUD           — heat/thirst bars, portrait
 *   5  WBP_InteractPrompt — "[E] NPC_NAME" prompt
 *  10  WBP_Dialogue       — right-panel dialogue box
 */
UCLASS()
class ECLIPSE_API AEclipseHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	// ── Assign these in BP_EclipseHUD defaults ──
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eclipse|UI")
	TSubclassOf<UEclipseInteractWidget> InteractWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eclipse|UI")
	TSubclassOf<UEclipseDialogueWidget> DialogueWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eclipse|UI")
	TSubclassOf<UEclipseHUDWidget> HUDWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eclipse|UI")
	TSubclassOf<UEclipseVnPortraitsWidget> VnPortraitsWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Eclipse|UI")
	TSubclassOf<UEclipseChapterCardWidget> ChapterCardWidgetClass;

	// ── Live references (read from BP if needed) ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<UEclipseInteractWidget> InteractWidget;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<UEclipseDialogueWidget> DialogueWidget;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<UEclipseHUDWidget> HUDWidget;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<UEclipseVnPortraitsWidget> VnPortraitsWidget;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|UI")
	TObjectPtr<UEclipseChapterCardWidget> ChapterCardWidget;
};
