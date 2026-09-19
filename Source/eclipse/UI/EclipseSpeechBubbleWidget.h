// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NPC/EclipseNpcCharacter.h"   // for EEclipseBubbleType
#include "EclipseSpeechBubbleWidget.generated.h"

class UTextBlock;
class UBorder;

/**
 * Small floating widget displayed above an NPC head via a UWidgetComponent.
 * The bubble shape and content come straight from the HTML prototype's .npc-bubble:
 *
 *   border: 2px solid #51eefc; border-radius: 16px;
 *   background: linear-gradient(180deg, #1e4a96 → #0f2c62);
 *   color: #f0f8ff;  font: RodinPro 14pt bold;
 *   triangular tail pointing down at the NPC.
 *
 * Set BubbleType to drive the symbol ("?" / "!" / "?!" / "…") or hide entirely
 * when None.
 */
UCLASS()
class ECLIPSE_API UEclipseSpeechBubbleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void SetBubble(EEclipseBubbleType InType, bool bMuted);

protected:
	virtual bool Initialize() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> BubbleBg;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BubbleText;
};
