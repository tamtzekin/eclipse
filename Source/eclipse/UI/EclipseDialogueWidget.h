// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "EclipseDialogueWidget.generated.h"

class UTextBlock;
class UScrollBox;
class UVerticalBox;
class UButton;
class UBorder;
class UImage;

/**
 * Right-side dialogue panel. Binds to EclipseDialogueSubsystem delegates.
 *
 * Mirrors the HTML #dialogue-box / #dialogue-choices layout:
 *   - Width 480px, right-anchored, full-height
 *   - Dark gradient background with dashed inner border
 *   - BMSPA speaker name, RodinPro body text, choice circles
 *
 * Blueprint child must contain these exact widget names:
 *   UTextBlock      SpeakerNameText
 *   UTextBlock      BodyText
 *   UVerticalBox    ChoicesBox
 *   UButton         CloseButton
 */
UCLASS()
class ECLIPSE_API UEclipseDialogueWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ── BindWidgetOptional — built programmatically in Initialize() if WBP empty ──
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpeakerNameText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BodyText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ChoicesBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	// Speaker portrait — sticks off the left edge of the dialogue panel,
	// slightly overlapping. Texture sourced from active NPC's PortraitTexture.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SpeakerPortrait;

	// ── Pre-built choice rows (slot 0..4) ───────────────────────────────────
	// These are populated into the WBP at design-time by EclipseUiBuilder so
	// you can edit the font / colors / layout visually. RebuildChoices() at
	// runtime just sets text + visibility on the existing buttons.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChoiceBtn_0;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChoiceBtn_1;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChoiceBtn_2;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChoiceBtn_3;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChoiceBtn_4;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChoiceText_0;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChoiceText_1;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChoiceText_2;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChoiceText_3;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChoiceText_4;

	// ── Sound assets (designer-assigned in WBP_Dialogue defaults) ──
	// All optional — if null, AudioSubsystem::PlayUI no-ops. Wire actual
	// sound waves once the audio assets land.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> DialogueOpenSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> DialogueChoiceSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> DialogueCloseSound;

private:
	UFUNCTION()
	void HandleDialogueOpened(AEclipseNpcCharacter* Npc);

	UFUNCTION()
	void HandleNodeChanged(FEclipseDialogueNodeView Node);

	UFUNCTION()
	void HandleDialogueClosed();

	UFUNCTION()
	void OnCloseClicked();

	// Choice callbacks — one per slot (slice cap: 5 choices)
	UFUNCTION() void OnChoice0();
	UFUNCTION() void OnChoice1();
	UFUNCTION() void OnChoice2();
	UFUNCTION() void OnChoice3();
	UFUNCTION() void OnChoice4();

	void MakeChoice(int32 Index);
	void RebuildChoices(const TArray<FEclipseDialogueChoice>& Choices);

	UPROPERTY()
	TArray<TObjectPtr<UButton>> ChoiceButtons;

	// Currently-highlighted choice for keyboard navigation. -1 if none.
	int32 SelectedIndex = 0;

	void HighlightChoice(int32 Index);
	void NavigateChoice(int32 Delta);   // +1 = next, -1 = prev

	// Keyboard input — W/S/Up/Down navigate, E/Enter confirm.
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;
};
