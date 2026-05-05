// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "EclipseVnPortraitsWidget.generated.h"

class UBorder;
class UTextBlock;
class UImage;

/**
 * Full-screen overlay that renders two large character portraits at the bottom
 * corners while a dialogue is open. Mirrors the HTML #vn-stage / .vn-portrait
 * (index.html L527+):
 *
 *   - 340×460 each, NPC at left, Player at right (right edge - 400px)
 *   - cross-fade based on who's speaking (active = bright + sharp,
 *     inactive = brightness 0.45, saturate 0.7, scale 0.97)
 *   - hidden when no dialogue active
 *
 * Until portrait art is imported, the slots show a coloured frame with the
 * speaker's name in BMSPA — enough to read the cross-fade behaviour.
 */
UCLASS()
class ECLIPSE_API UEclipseVnPortraitsWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> NpcPortrait;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> NpcLabel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> PlayerPortrait;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PlayerLabel;

private:
	UFUNCTION() void HandleOpened(class AEclipseNpcCharacter* Npc);
	UFUNCTION() void HandleNodeChanged(FEclipseDialogueNodeView Node);
	UFUNCTION() void HandleClosed();

	void SetActive(UBorder* Frame, UTextBlock* Label, bool bActive);
};
