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
class UWrapBox;
class UAudioComponent;
struct FEclipseDialogueChoice;

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

	// Word-by-word fade-in container. When bound, body text is rendered as a
	// wrap-box of per-word UTextBlocks whose alpha is animated in NativeTick.
	// If null at runtime we fall back to setting BodyText (legacy single block).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> BodyWords;

	// Orange one-liner shown right below the body, summarising any stage-
	// directive effects on the fragment (e.g. "-1 AESTHETICS  ·  +2 ENERGY").
	// Built programmatically in NativeConstruct if the WBP doesn't ship one
	// (parallels the BodyWords / HUD runtime-injection pattern).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EffectsLineText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ChoicesBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CloseButton;

	// Speaker portrait — sticks off the left edge of the dialogue panel,
	// slightly overlapping. Texture sourced from active NPC's PortraitTexture.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SpeakerPortrait;

	// ── Speech-bubble history (Visual-Novel L/R stacking) ───────────────
	// Two scroll boxes anchored to the screen edges. The player's chosen
	// lines stack on the LEFT, the NPC's spoken lines stack on the RIGHT.
	// Each entry is a UBorder ("bubble") with a UWrapBox of per-word
	// UTextBlocks inside, so the existing word-by-word fade animation can
	// target the newest bubble's wrap box via BodyWords (redirected each
	// time a new NPC line lands). Cleared on HandleDialogueClosed.
	//
	// Both are runtime-injected in NativeConstruct if the bound WBP didn't
	// ship them, mirroring the BodyWords / EffectsLineText pattern.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> LeftHistoryScroll;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> RightHistoryScroll;

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

	// "Mumble" voice — sliced into syllable-sized chunks as each dialogue word
	// fades in. Default loaded from /Game/Audio/angel_voice if this is null at
	// NativeConstruct time (so designers don't have to wire it up manually).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio|Mumble")
	TObjectPtr<class USoundBase> DialogueMumbleSound;

	// Min/max length of each splice, seconds. Longer than the trigger spacing
	// (3 words × 0.11s = 0.33s) so consecutive slices substantially overlap —
	// the AudioSubsystem fades each slice out over MumbleSliceFadeOutSeconds
	// so the overlap reads as a smooth crossfade between melody fragments.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumbleSliceMinSeconds = 0.55f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumbleSliceMaxSeconds = 0.85f;

	// Tail fade applied at the end of every slice. A longer tail makes
	// consecutive slices crossfade smoothly into a continuous vocal-melody
	// feel; a short tail (~0.04) gives a more obvious chopped-up texture.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumbleSliceFadeOutSeconds = 0.18f;

	// Fire one slice every Nth word, not every word. 1 = chatter (per-word),
	// 3 = phrase-feel (default — matches "mumble per few words" pacing), 5+
	// would feel sparse. Counted across the global word stream (body + choices).
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble", meta = (ClampMin = "1", ClampMax = "10"))
	int32 MumbleWordsPerSlice = 3;

	// Pitch range — keep tight (1.0 / 1.0) so the underlying melody of the
	// source clip survives the slicing. Spread these out (e.g. 0.85 / 1.25)
	// for a more stylised "garbled mumble" feel that breaks pitch continuity.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumblePitchMin = 1.0f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumblePitchMax = 1.0f;

	// Volume multiplier for each mumble slice. The angel_voice asset is loud
	// at full pitch — keep this below 1 so it sits under any music.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumbleVolume = 0.6f;

	// Approximate playable length of the source clip (seconds). Each slice's
	// random StartTime is drawn from [0, MumbleSourceLength - MaxSlice]. The
	// shipped angel_voice.uasset is ~20.75 s — leave the default a touch
	// shorter so we never run off the end. Override per-WBP if you swap clips.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Audio|Mumble")
	float MumbleSourceLength = 20.0f;

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

	// ── Bubble construction helpers ─────────────────────────────────────
	//
	// Appends a new black semi-transparent bubble to one of the history
	// scroll boxes. Returns the inner UWrapBox so the caller can drive the
	// per-word fade-in (HandleNodeChanged redirects `BodyWords` here so
	// StartBodyAnimation lands on the new bubble). The speaker caption
	// renders above the words inside the bubble.
	//
	// EffectsOut, when non-null, receives a UTextBlock the caller can fill
	// with the orange stage-direction effects line (sits below the words
	// inside the same bubble; collapsed by default).
	class UWrapBox* AppendBubble(class UScrollBox* Box,
	                             const FText& SpeakerCaption,
	                             const FLinearColor& CaptionTint,
	                             bool bAlignRight,
	                             class UTextBlock** EffectsOut = nullptr);

	// Caches the choices passed to the most recent RebuildChoices so
	// MakeChoice can grab the chosen line's text and stamp a player-side
	// bubble into LeftHistoryScroll before the dialogue advances to the
	// next node.
	UPROPERTY()
	TArray<FEclipseDialogueChoice> CurrentChoices;

	UPROPERTY()
	TArray<TObjectPtr<UButton>> ChoiceButtons;

	// Currently-highlighted choice for keyboard navigation. -1 if none.
	int32 SelectedIndex = 0;

	void HighlightChoice(int32 Index);
	void NavigateChoice(int32 Delta);   // +1 = next, -1 = prev

	// Keyboard input — W/S/Up/Down navigate, E/Enter confirm.
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	// ── Word-by-word fade-in animation state ──
	struct FDlgWord
	{
		TObjectPtr<UTextBlock> Block;
		float SpawnDelay = 0.f;   // seconds from anim start
	};

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> AnimWordBlocks;   // for GC root
	TArray<float> AnimWordDelays;                    // parallel to AnimWordBlocks
	TArray<FLinearColor> AnimWordTints;              // parallel — per-word target colour
	TArray<bool>  AnimWordMumbleFired;               // parallel — guard for one-shot mumble per word

	// Per-choice reveal timing: each button stays Collapsed until DialogueAnimTime
	// reaches its corresponding reveal-time, then snaps to Visible. This is what
	// keeps the choice rows from popping in alongside the body — they only
	// surface after the body has finished cascading.
	UPROPERTY()
	TArray<TObjectPtr<UButton>> ChoiceRevealButtons;
	TArray<float> ChoiceRevealDelays;

	float DialogueAnimTime  = 0.f;
	bool  bDialogueAnimating = false;

	// Cursor through the mumble source clip. Advances by each slice's duration
	// so consecutive slices play sequential chunks of the file — preserves the
	// underlying melody. Wraps to 0 once the cursor would overrun the clip.
	float MumbleCursor = 0.f;

	// Live mumble slices spawned by PlayMumbleSlice. Tracked so we can cut
	// them all off the instant the dialogue text finishes animating — without
	// this they'd keep playing past the visible body, which feels wrong (the
	// voice should stop the moment the text is fully revealed).
	TArray<TWeakObjectPtr<UAudioComponent>> ActiveMumbleSlices;

	// Total wall-clock seconds the body animation will take (used to delay
	// the choice rows so they cascade in after the body completes).
	float BodyAnimTotalTime = 0.f;

	// Tunables — slower than the chat-typer default so the prose has weight.
	// Designer can tweak these per-WBP without a recompile.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue|Animation")
	float WordSpawnInterval = 0.11f;   // ~9 words/sec — readable, not breathless

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue|Animation")
	float WordFadeDuration  = 0.32f;   // each word fades over ~320ms

	void StartBodyAnimation(const FString& BodyString);

	// Adds word-by-word fade-in to a single choice row. Replaces the static
	// PreText label with a UWrapBox of per-word UTextBlocks (or finds an
	// existing one created on a previous run) and registers each word in
	// AnimWordBlocks with the supplied start-delay + per-word stagger.
	void AnimateChoiceText(class UTextBlock* Label, int32 ChoiceIndex,
	                       const FString& Text, const FLinearColor& TargetTint,
	                       float StartDelay);

	// Fires a single random-pitch slice of DialogueMumbleSound. Picks a random
	// StartTime within the source clip, a random duration between
	// MumbleSliceMin/MaxSeconds, and a random pitch in MumblePitchMin/Max.
	void PlayMumbleSlice();
};
