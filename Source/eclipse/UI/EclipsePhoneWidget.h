// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipsePhoneWidget.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UScrollBox;
class UVerticalBox;

/**
 * Left-edge phone overlay (P key toggle). Ports the prototype's
 * `#phone-ui` panel from the Three.js prototype — 260 wide, vertically
 * centred, navy gradient with cyan accents. The clock + chapter readouts
 * live ON the phone face (moved off the HUD per the slice TODO); the
 * status row also shows the player's wallet (coins · notes).
 *
 * MVP layout, top to bottom:
 *   - Header strip:   "PHONE"  ·  close ×
 *   - Status row:     CLOCK (top-left)  ·  WALLET (top-right)
 *   - Tab row:        CONTACTS  /  NOTES
 *   - Scrolling content area (placeholder until contacts + notes ship)
 *   - Actions row:    CALL  ·  TEXT     (both disabled in MVP)
 *
 * Pattern mirrors UEclipseInventoryWidget:
 *   - BindWidgetOptional pointers so a designer can ship a WBP_Phone
 *     later without touching C++.
 *   - C++ tree fallback built in Initialize() when no WBP is bound.
 *   - Static OpenForPlayer(PC) factory + Close() that pauses the world,
 *     flips input to UI-only, and shows the cursor.
 *   - Pulls the clock + wallet from UEclipseGameStateSubsystem each
 *     NativeTick so the phone face stays current in real time.
 */
UCLASS()
class ECLIPSE_API UEclipsePhoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Factory used by the player controller's TogglePhone handler. Loads
	// /Game/Justin/UI/WBP_Phone if present, otherwise falls back to the
	// C++ tree built by Initialize().
	static UEclipsePhoneWidget* OpenForPlayer(class APlayerController* PC);

	// Pulled by the player controller when P is pressed a second time.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Phone")
	void Close();

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	// ── Bound widgets (designer or C++ fallback). All optional so the
	// fallback tree is the only required path; a WBP can selectively
	// replace any subset. ───────────────────────────────────────────────
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>  ClockText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>  ChapterLabelText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>  WalletText;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>     ContactsTabBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>     NotesTabBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UScrollBox>  ContentScroll;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>  ContentPlaceholder;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>     CallBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>     TextBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>     CloseBtn;

	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	UFUNCTION() void OnCloseClicked();
	UFUNCTION() void OnContactsTabClicked();
	UFUNCTION() void OnNotesTabClicked();

	// Active tab — MVP just swaps the placeholder text; full content lists
	// arrive when contacts/notes ship as proper features.
	enum class EPhoneTab : uint8 { Contacts, Notes };
	EPhoneTab ActiveTab = EPhoneTab::Contacts;

	void RefreshFace();           // clock + wallet
	void ApplyTab(EPhoneTab Tab); // visual tab swap + placeholder text
};
