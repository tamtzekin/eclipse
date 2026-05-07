// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipsePauseMenuWidget.generated.h"

class UVerticalBox;
class UTextBlock;
class UButton;
class UBorder;

/**
 * Esc-triggered pause overlay. Pauses the world via UGameplayStatics::SetGamePaused
 * and exposes Resume / Save / Load / Main Menu / Quit. Mirrors the chalk-on-slate
 * style of the dialogue panel — same fonts, same border treatment.
 *
 * The PlayerController binds Escape to ToggleVisible() — the widget creates
 * itself on demand (no need to keep it on the viewport while invisible) so
 * gameplay-time CPU is zero.
 */
UCLASS()
class ECLIPSE_API UEclipsePauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Static helper: create + show the pause menu over the local player. Returns the widget. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipsePauseMenuWidget* OpenForPlayer(APlayerController* PC);

	/** Closes + destroys the menu, unpauses, restores game input mode. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ResumeBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SaveBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> LoadBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> MainMenuBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> QuitBtn;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;

	// ── Main list / slot picker ── separate vertical boxes inside the panel,
	// only one visible at a time. SlotPicker contains the 3 slot rows + a back
	// button, hidden by default; ShowSlotPicker(true) reveals it.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<class UVerticalBox> MainList;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<class UVerticalBox> SlotPicker;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SlotPickerTitle;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot0Btn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot1Btn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> Slot2Btn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> SlotBackBtn;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Slot0Btn_Label;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Slot1Btn_Label;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Slot2Btn_Label;

	// Level to open when Main Menu is clicked. Soft-pointer so we don't pull
	// the menu level into every other level's cooked content.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|UI")
	FName MainMenuLevelName = TEXT("L_MainMenu");

private:
	UFUNCTION() void OnResume();
	UFUNCTION() void OnSave();
	UFUNCTION() void OnLoad();
	UFUNCTION() void OnMainMenu();
	UFUNCTION() void OnQuit();

	UFUNCTION() void OnSlot0();
	UFUNCTION() void OnSlot1();
	UFUNCTION() void OnSlot2();
	UFUNCTION() void OnSlotBack();

	void HandleSlot(int32 SlotIndex);
	void RefreshSlotLabels();
	void ShowSlotPicker(bool bSaveMode);
	void ShowMainList();

	void SetStatus(const FString& Msg);

	// Build the widget tree if the WBP didn't ship one — same fallback
	// pattern as DialogueWidget / HUDWidget.
	void BuildFallbackTree();

	// Sub-state for the slot picker. true = Save mode (writes), false = Load mode (reads).
	bool bSlotMode = false;
	bool bSaveMode = true;
};
