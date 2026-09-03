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
/** A run of body text attributed to one speaker — see ParseSpeechSegments. */
struct FEclipseSpeechSegment
{
	FName   Speaker;
	FString Text;
};

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

	// Speaker portrait — top-left of the screen, next to SpeakerNameText.
	// Texture sourced from active NPC's PortraitTexture. Visible only
	// during the NPC's "turn" (see HandleNodeChanged / MakeChoice) —
	// hidden the instant the player commits a choice, re-shown when the
	// NPC's next line arrives.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> SpeakerPortrait;

	// Whether the active NPC actually has a portrait texture assigned.
	// Turn-based show/hide only re-shows the portrait when this is true —
	// otherwise an NPC with no portrait would flash an empty frame every
	// time it's "their turn" again.
	bool bSpeakerHasPortrait = false;

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

	// How long a sentence's caption box gets to lay itself out before the
	// first word inside it starts fading in. Needs to cover at least one
	// frame — see StartBodyAnimation for why. Long enough to be reliable at
	// low framerates, short enough to read as the same beat.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float SentenceBoxLayoutLead = 0.05f;

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

	// ── Transcript box layout ──────────────────────────────────────────────
	// Screen-space anchors (0-1) for the scrollable dialogue box (right
	// third of the viewport). Edit these on the WBP's Class Defaults to
	// resize/reposition the box without touching C++.
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue|Layout")
	float BoxTopAnchor = 0.18f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue|Layout")
	float BoxBottomAnchor = 0.75f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|Dialogue|Layout")
	float BoxRightMargin = 12.f;

private:
	UFUNCTION()
	void HandleDialogueOpened(AEclipseNpcCharacter* Npc);

	UFUNCTION()
	void HandleNodeChanged(FEclipseDialogueNodeView Node);

	// The actual work HandleNodeChanged used to do unconditionally. Split out
	// so a node that arrives while the player's own "YOU" line is still
	// mid-cascade (MakeChoice sets bPlayerLineAnimating before calling into
	// the subsystem, so this always fires synchronously on the same click)
	// can be held back — NativeTick applies it once bPlayerLineAnimating
	// clears, so the NPC's name/portrait/line only start appearing after the
	// player's line has finished, never at the same time.
	void ApplyNodeChanged(const FEclipseDialogueNodeView& Node);

	UFUNCTION()
	void HandleDialogueClosed();

	// Injects a mint-green "ZEN: +20 XP" system line into the transcript
	// whenever GrantStatXP fires (skill-check clicks during dialogue).
	UFUNCTION()
	void HandleStatXPGranted(FName StatKey, int32 Amount, int32 NewLevel, bool bLeveledUp);

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

	// Base tint per choice row (parallel to ChoiceButtons). NativeTick's
	// hover pass paints the row's text white while hovered or selected,
	// and restores this base tint otherwise.
	TArray<FLinearColor> ChoiceBaseTints;

	// How many leading (whitespace-split) words of each choice row are the
	// stat-check label ("Aesthetics [3]:") rather than the actual choice
	// wording — parallel to ChoiceButtons/ChoiceBaseTints. NativeTick's hover
	// pass skips these when recolouring a row, so the label keeps its stat
	// hue permanently instead of flashing to the hover/base colour.
	TArray<int32> ChoiceLabelWordCounts;

	// Per-slot word-wrap box for the animated choice text (parallel to
	// ChoiceButtons), indexed directly instead of re-found each call via
	// WidgetTree->FindWidget("ChoiceWords_%d") — a persistent, name-based
	// lookup that could resolve to a stale widget across RebuildChoices
	// calls (observed as choice button text lagging behind the real,
	// already-correct choice data by however many turns since that slot was
	// last active). Reset+repopulated by RebuildChoices; used by
	// AnimateChoiceText.
	TArray<TObjectPtr<class UWrapBox>> ChoiceWordBoxes;

	// Diamond bullet per choice row, and how far each row has cross-faded
	// into its hovered look (0 = resting white card, 1 = black).
	TArray<TWeakObjectPtr<class UBorder>> ChoiceDiamonds;
	TArray<float> ChoiceHoverAlphas;

	// Bullets are held at zero opacity with their rows — a child that sets
	// its own render transform doesn't inherit the button's opacity ramp.
	TArray<TWeakObjectPtr<class UWidget>> ChoiceRevealDiamonds;

	// How many caption bubbles the history keeps. Sized so the newest is
	// always whole rather than clipped by the scroll box's top edge.
	UPROPERTY(EditAnywhere, Category = "Eclipse|Dialogue")
	int32 MaxHistoryBubbles = 3;

	// Drops whole bubbles off the top until the transcript fits its own
	// viewport. A count cap can't do this on its own — bubbles differ in
	// height, so three tall ones still overflow and the oldest gets sliced
	// through the middle, which reads as a rendering fault.
	void TrimHistoryToFit();

	// Background card per choice row (parallel to ChoiceButtons) — white
	// bg / black text at rest, flips to black bg / white text on hover or
	// keyboard selection (matching the NPC caption boxes' own black card).
	TArray<TObjectPtr<UBorder>> ChoiceRowBackgrounds;

	// Label per choice row (parallel to ChoiceButtons/ChoiceRowBackgrounds).
	// RebuildChoices constructs both fresh from C++ every call rather than
	// reusing/hiding the WBP's own baked ChoiceRow_i/ChoiceBg_i/ChoiceText_i
	// sub-widgets — that reuse path proved unreliable no matter how it was
	// hidden/re-shown (design-time placeholder text and card backgrounds
	// kept bleeding through). Tracked directly here — not re-found by name
	// each frame — for the same reason ChoiceWordBoxes moved off
	// WidgetTree->FindWidget: a reused/collided name can resolve to a
	// stale widget instead of the current one.
	TArray<TObjectPtr<class UTextBlock>> ChoiceLabelWidgets;

	// Parallel to ChoiceLabelWidgets — the separate, stat-coloured
	// "Aesthetics [3]" / "[AESTHETICS 3]" prefix block that sits before the
	// body text in the row. Null for choices with no stat-check label. Kept
	// apart from the body so ONLY the stat name and its threshold take the
	// hue; the colon and the choice's own wording stay neutral.
	UPROPERTY()
	TArray<TObjectPtr<class UTextBlock>> ChoicePrefixLabels;

	// Phase driver for the subtle hover pulse (NativeTick's hover pass) —
	// free-running seconds, not reset per node.
	float ChoicePulseTime = 0.f;

	// Resolves the display tint for a choice row. Skill-check choices are
	// colour-coded by stat (pink=AESTHETICS, gold=RHYTHM, blue=ZEN,
	// violet=PSYCHEDELICS) — plain cream at levels 1-2, blending toward
	// the stat hue from level 3 up (full saturation ~level 9). A vivid
	// violet option reads "my Psychedelics is high" at a glance.
	// Non-skill choices keep cream (red when gate-blocked).
	FLinearColor ChoiceTint(const struct FEclipseDialogueChoice& Choice) const;

	// ── Caption-row construction helpers ────────────────────────────────
	//
	// Appends a new closed-caption-style row to one of the history scroll
	// boxes: a speaker-caption label above a UWrapBox of individually
	// black-boxed word chips (no chat-bubble container, no tail). Returns
	// the inner UWrapBox so the caller can drive the word reveal
	// (HandleNodeChanged redirects `BodyWords` here so StartBodyAnimation
	// lands in the new row's wrap box).
	//
	// EffectsOut, when non-null, receives a UTextBlock the caller can fill
	// with the orange stage-direction effects line (sits below the words;
	// collapsed by default).
	//
	// bAlignRight anchors the whole row to the right edge of the transcript
	// column instead of the left — NPC lines stay left-aligned, the
	// player's own lines are pushed right, so a turn's speaker is readable
	// at a glance even with the boxes' near-full-width stagger.
	class UWrapBox* AppendBubble(class UScrollBox* Box,
	                             const FText& SpeakerCaption,
	                             const FLinearColor& CaptionTint,
	                             class UTextBlock** EffectsOut = nullptr,
	                             bool bAlignRight = false);

	// Splits a body/choice string into "sentences" — runs of words up to
	// and including one ending in . ! or ? (tolerating a trailing quote or
	// paren after the mark, e.g. `crying."`). A trailing run with no
	// terminal punctuation is still returned as its own group. Drives the
	// per-sentence caption background box below (one box per sentence, not
	// per word).
	static TArray<TArray<FString>> SplitIntoSentences(const FString& Text);

	// One closed-caption "sentence box": a black rounded background that
	// spans however many lines its words wrap to, plus the inner UWrapBox
	// the caller populates with that sentence's bare (unboxed) word
	// UTextBlocks.
	struct FSentenceBox
	{
		class UBorder*  Box   = nullptr;
		class UWrapBox* Inner = nullptr;
	};

	// Builds one FSentenceBox and appends it to Parent, reserving a full-row
	// SizeBox "slot" (WidthOverride = WrapWidth) so the NEXT sentence is
	// still forced onto a new line. The visible Border inside that slot
	// stays pinned to the row's speaker-side edge (bAlignRight false = left,
	// for NPC rows; true = right, for the player's rows) — no left/right
	// swing — but every other sentence (SentenceIndex parity) gets a small
	// ~1-2-letter nudge further in from that edge, just enough to read as a
	// subtle stagger on an otherwise continuous block of text. bIsFirst/
	// bIsLast square off the corners that touch the neighbouring box above/
	// below (InnerSlotPadding between boxes is 0 — see AppendBubble) so
	// consecutive boxes read as one joined strip; only the very first box
	// keeps rounded top corners and the very last keeps rounded bottom
	// corners. Box starts Collapsed; the caller reveals it (SetVisibility)
	// the moment the sentence's first word becomes due, and it stays
	// Visible (growing) as later words in the same sentence reveal.
	FSentenceBox BeginSentenceBox(class UWrapBox* Parent, float WrapWidth,
	                               int32 SentenceIndex, bool bIsFirst, bool bIsLast,
	                               bool bAlignRight = false);

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

	// True once the player has actually navigated via keyboard (arrows/WASD/
	// number keys) — gates the visual highlight so choice 0 doesn't appear
	// pre-selected the instant a new set of choices appears. SelectedIndex
	// still defaults to 0 underneath so Enter/E confirms the first choice
	// even if the player never touches the keyboard nav.
	bool bKeyboardSelectionActive = false;

	void HighlightChoice(int32 Index);
	void NavigateChoice(int32 Delta);   // +1 = next, -1 = prev

	// Keyboard input — W/S/Up/Down navigate, E/Enter confirm.
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	// Mouse wheel over ANY part of the dialogue panel scrolls the transcript
	// — bubbles up from whatever child is under the cursor to this root
	// widget, so it isn't limited to hovering the scrollbox's own narrow
	// hit-test area. Eased toward a target offset in NativeTick rather than
	// snapping instantly (see DialogueScrollTargetOffset).
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	float DialogueScrollTargetOffset = 0.f;
	bool  bDialogueScrollTargetActive = false;

	// ── Word-by-word reveal animation state ──
	// AnimWordBlocks holds the REVEAL TARGET for each word — the whole
	// black caption-chip UBorder for body words, or the bare UTextBlock for
	// choice words (which aren't chipped, see AnimateChoiceText). Revealing
	// a word is a single Visibility flip (Collapsed → Visible) at its
	// scheduled delay — no per-frame colour or alpha interpolation, no
	// fade. This is what's animated: WHEN each word/chip pops in, not HOW.
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> AnimWordBlocks;      // for GC root
	TArray<float> AnimWordDelays;                    // parallel to AnimWordBlocks
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

	// Same word-reveal mechanism as AnimWordBlocks/AnimWordDelays above, but
	// entirely separate state — the player's echoed "YOU" line has to keep
	// animating independently of whatever the NPC's next line is doing,
	// since MakeChoice appends the player's row and then immediately
	// dispatches to the next node (which resets AnimWordBlocks for itself).
	UPROPERTY()
	TArray<TObjectPtr<UWidget>> PlayerAnimWordBlocks;
	TArray<float> PlayerAnimWordDelays;
	float PlayerAnimTime = 0.f;
	bool  bPlayerLineAnimating = false;

	// Node held back by HandleNodeChanged while bPlayerLineAnimating is true —
	// applied by NativeTick once the player's line has finished AND this
	// timer clears PendingNodeDelay, so the NPC's turn doesn't snap in the
	// instant the player's line stops (see NativeTick's "held-back NPC turn"
	// block).
	TOptional<FEclipseDialogueNodeView> PendingNode;
	float PendingNodeTimer = 0.f;
	static constexpr float PendingNodeDelay = 0.35f;

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

	void StartBodyAnimation(const FString& BodyString);

	// One speaker's stretch of a node's body. See ParseSpeechSegments.
	void BuildBodyWords(const FString& BodyString, float& RunningDelay);
	void FinishBodyAnimation();
	static TArray<struct FEclipseSpeechSegment> ParseSpeechSegments(
		const FString& Body, FName DefaultSpeaker);
	void StartBodySegments(const TArray<struct FEclipseSpeechSegment>& Segments);
	class AEclipseNpcCharacter* FindNpcByDialogueId(FName Id) const;

	// When each speaker's stretch starts printing, and where they stand.
	// Parallel arrays walked forward by NativeTick to swing the camera.
	TArray<float>   SegmentStartDelays;
	TArray<FVector> SegmentSpeakerLocations;
	int32           NextSegmentFocus = 0;

	// Mirrors bDialogueAnimating onto the dialogue subsystem so non-dialogue
	// UI (the HUD quest checklist) can hold off until the line has finished
	// printing. See UEclipseDialogueSubsystem::IsBodyPrinting.
	void SetBodyPrintingFlag(bool bPrinting);

	// One choice per node. Set the moment a choice is dispatched, cleared
	// when the next node's choices are on screen. Without it, mashing E
	// fires MakeChoice repeatedly against a CurrentChoices array that hasn't
	// been replaced yet, and the same option gets taken twice.
	bool bChoiceCommitted = false;

	// "Quick skip": finish the current line instantly instead of waiting out
	// the word cascade. Bound to a SECOND press of E/Enter/Space — the first
	// press commits the choice, the second says "I've read it, move on".
	void SkipToChoices();

	// Adds word-by-word fade-in to a single choice row. Replaces the static
	// PreText label with a UWrapBox of per-word UTextBlocks (or finds an
	// existing one created on a previous run) and registers each word in
	// AnimWordBlocks with the supplied start-delay + per-word stagger.
	// LabelWordCount leading (whitespace-split) words use LabelTint instead
	// of TargetTint — the stat-check label prefix ("Aesthetics [3]:"), kept
	// visually distinct from the rest of the choice's own wording.
	void AnimateChoiceText(class UTextBlock* Label, int32 ChoiceIndex,
	                       const FString& Text, const FLinearColor& TargetTint,
	                       float StartDelay, int32 LabelWordCount = 0,
	                       const FLinearColor& LabelTint = FLinearColor::Black);

	// Fires a single random-pitch slice of DialogueMumbleSound. Picks a random
	// StartTime within the source clip, a random duration between
	// MumbleSliceMin/MaxSeconds, and a random pitch in MumblePitchMin/Max.
	void PlayMumbleSlice();
};
