// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseDialogueSubsystem.generated.h"

class AEclipseNpcCharacter;
class AEclipseItemActor;
class UInkpotStory;

// ── Stage directives (authored as Ink tags) ──────────────────────────────────
// Parsed from a comma-separated string of tokens built by joining the Ink
// tags on a line/choice (see InkSource/*.ink). Four kinds:
//   [STAT_NAME: N]   StatGate     — gates choice availability (greys it out
//                                   with a "(need STAT N)" hint when the
//                                   player's stat is below N).
//   [ITEM_NAME]      ItemGate     — gates choice availability against the
//                                   inventory. ITEM_NAME is the DT_Items row
//                                   id in ALL_CAPS (matched case-insensitively
//                                   against the lowercased inventory id).
//   +N STAT_NAME     StatEffect   — applied on click; STAT_NAME is one of
//   -N STAT_NAME                    AESTHETICS / RHYTHM / ZEN / PSYCHEDELICS.
//                                   Lowercased to a key.
//   +N METER_NAME    MeterEffect  — applied on click; METER_NAME is one of
//   -N METER_NAME                   HEAT / THIRST / STIMULATION. Signed delta
//                                   on the 0..10 meter scale.
//   METER OP N       MeterCompareGate — gates choice availability. METER is
//                                       HEAT / THIRST / STIMULATION; OP is
//                                       one of < > <= >= == !=. e.g.
//                                       "HEAT > 8", "STIMULATION < 3".
//   GENDER == female    IdentityGate  — gates choice availability on a hidden
//   RACE != brown                       identity tag (GENDER / RACE). Only ==
//                                        and != are meaningful. Hidden: a
//                                        failed gate just closes the option,
//                                        with no revealing "why" hint.
//   ANNOYANCE OP N      HiddenStatGate — numeric gate on a hidden social stat
//                                        (currently ANNOYANCE 0..10). e.g.
//                                        "ANNOYANCE >= 4" unlocks deeper trees.
//   +N ANNOYANCE        HiddenStatEffect — moves a hidden numeric stat on click.
//   -N ANNOYANCE
UENUM(BlueprintType)
enum class EEclipseStageDirectiveKind : uint8
{
	StatGate,
	ItemGate,
	StatEffect,
	MeterEffect,
	MeterCompareGate,
	IdentityGate,       // GENDER / RACE name == / != comparison
	HiddenStatGate,     // ANNOYANCE numeric compare
	HiddenStatEffect,   // +N / -N ANNOYANCE
	StatCompareGate,    // "ZEN >= 3" — silent unlock on a visible stat: the
	                    // choice is REMOVED (not greyed) until the level is
	                    // reached, so new options simply appear as you grow.
};

UENUM(BlueprintType)
enum class EEclipseCompareOp : uint8
{
	Less,           // <
	LessEqual,      // <=
	Equal,          // ==
	NotEqual,       // !=
	GreaterEqual,   // >=
	Greater,        // >
};

USTRUCT(BlueprintType)
struct FEclipseStageDirective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) EEclipseStageDirectiveKind Kind = EEclipseStageDirectiveKind::StatGate;
	// Lowercase stat / meter / hidden key, depending on Kind:
	//   StatGate / StatEffect                → "aesthetics" / "rhythm" / "zen" / "psychedelics"
	//   MeterEffect / MeterCompareGate       → "heat" / "thirst"
	//   HiddenStatGate / HiddenStatEffect    → "annoyance"
	//   IdentityGate                         → "gender" / "race"
	//   ItemGate                             → empty
	UPROPERTY(BlueprintReadOnly) FName Stat;
	// For ItemGate: lowercased DT_Items row id (e.g. "baggie", "empty_bottle").
	// For IdentityGate: the lowercased right-hand identity value to compare
	// against (e.g. "female", "brown").
	UPROPERTY(BlueprintReadOnly) FName ItemId;
	// Multi-purpose:
	//   StatGate                         → required threshold (player STAT must be ≥)
	//   StatEffect / MeterEffect /
	//   HiddenStatEffect                 → signed delta
	//   MeterCompareGate / HiddenStatGate → right-hand side of the comparison
	//   IdentityGate                     → unused (RHS is a name in ItemId)
	UPROPERTY(BlueprintReadOnly) int32 Value = 0;
	// Only used by MeterCompareGate. Defaults to >= so a stray non-op'd
	// comparison still behaves like the old "threshold" gate.
	UPROPERTY(BlueprintReadOnly) EEclipseCompareOp Op = EEclipseCompareOp::GreaterEqual;
};

USTRUCT(BlueprintType)
struct FEclipseDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName ChoiceId;
	UPROPERTY(BlueprintReadOnly) FText Text;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = true;
	// True when a HIDING gate failed (IdentityGate / HiddenStatGate /
	// StatCompareGate). Such choices are dropped from the node view
	// entirely — the player never sees them, no greyed row, no hint.
	UPROPERTY(BlueprintReadOnly) bool bHiddenGateFailed = false;
	UPROPERTY(BlueprintReadOnly) bool bIsSkillCheck = false;
	UPROPERTY(BlueprintReadOnly) FName SkillCheckStat;   // "aesthetics" | "rhythm" | "zen" | "psychedelics"
	UPROPERTY(BlueprintReadOnly) int32 SkillCheckValue = 0;
	// Heat cost when this choice is a failed skill check that the player
	// clicks anyway. Surfaced to the widget so it can render a
	// "[-N HEAT]" risk hint, consumed in MakeChoice via the
	// GameStateSubsystem ChangeHeat API. Heat is the meter that kills at
	// 0, so botching checks is what puts the player in danger.
	UPROPERTY(BlueprintReadOnly) int32 HeatDamageOnFail = 2;

	// All stage directives parsed from this choice's Ink tags. Gates are
	// evaluated when the choice is built (sets bAvailable + GateHint);
	// effects fire in MakeChoice when the player clicks.
	UPROPERTY(BlueprintReadOnly) TArray<FEclipseStageDirective> StageDirectives;

	// Human-readable hint about WHY this choice is unavailable, generated
	// from any gate directive that failed. Empty when available.
	// Examples: "(need ZEN 1)", "(no BAGGIE)".
	UPROPERTY(BlueprintReadOnly) FText GateHint;

	// Set when this choice's raw .ink line starts with a native Ink
	// conditional gate — "* {aesthetics > 3} [...]" — found by matching the
	// compiled choice text back against the .ink source (see
	// UEclipseDialogueSubsystem::FindInkGateLabel; Ink itself doesn't expose
	// the condition at runtime, only whether the choice passed it). Display
	// Ink already silently hides the choice if the condition is false, so
	// every choice that reaches here satisfied its gate — but "satisfied"
	// isn't the same as "passed the check": authors write matched pairs
	// like "{rhythm > 3} Hide it." / "{rhythm < 3} Hide it." where the
	// second is the FAILURE branch. bStatCheckLabelIsPass records which,
	// off the gate's comparison operator, so MakeChoice grinds the stat at
	// the right rate instead of rewarding the fallback as a success.
	UPROPERTY(BlueprintReadOnly) bool bHasStatCheckLabel = false;
	UPROPERTY(BlueprintReadOnly) FName StatCheckLabelStat;
	UPROPERTY(BlueprintReadOnly) int32 StatCheckLabelValue = 0;
	UPROPERTY(BlueprintReadOnly) bool bStatCheckLabelIsPass = true;

	// True for a pacing beat rather than a real decision — set when a
	// hand-authored Ink choice's display text is exactly "[CONTINUE]"
	// (see BuildChoicesFromStory). Nothing synthesises these anymore; the
	// author writes `* [CONTINUE]` in the .ink source wherever a stretch of
	// prose should pause. The widget uses this to skip the usual "YOU"
	// bubble + player-line animation, so a pacing click doesn't read as
	// something the player said.
	UPROPERTY(BlueprintReadOnly) bool bIsContinuePrompt = false;
};

USTRUCT(BlueprintType)
struct FEclipseDialogueNodeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName SpeakerName;
	UPROPERTY(BlueprintReadOnly) FText Body;
	UPROPERTY(BlueprintReadOnly) TArray<FEclipseDialogueChoice> Choices;

	// Pre-formatted orange line summarising the body fragment's effect
	// directives. Empty when the body has no effect directives. Example:
	// "−1 AESTHETICS · +2 ENERGY". The dialogue widget renders this in a
	// distinct orange tint on a new line below the body.
	UPROPERTY(BlueprintReadOnly) FText EffectsLine;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseDialogueOpened,      AEclipseNpcCharacter*, Npc);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseDialogueNodeChanged, FEclipseDialogueNodeView, Node);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipseDialogueClosed);

/**
 * Inkpot (Ink) runtime wrapper. Drives a single compiled story asset
 * (/Game/Justin/Dialogue/DA_MainStory, compiled from Content/Justin/Dialogue/
 * InkSource/Main.ink) shared by every NPC and interactable item.
 *
 * Responsibilities:
 *   - Load the one UInkpotStoryAsset on Init and keep a single live
 *     UInkpotStory for the whole session.
 *   - AEclipseNpcCharacter::DialogueId / AEclipseItemActor::DialogueId hold
 *     an Ink knot name (e.g. "angel_seeker", "red_wristband") — OpenDialogue
 *     / OpenItemDialogue jump there via Story->ChoosePath and pull a node
 *     view from wherever the story ends up (BuildNodeFromStory).
 *   - Gate/effect directives ("+1 RHYTHM", "HEAT > 8", ...) are authored as
 *     Ink tags on the relevant line/choice — same grammar as before, see
 *     ParseStageDirections below, just sourced from
 *     UInkpotStory::GetCurrentTags() / UInkpotChoice::GetTags() instead of
 *     an Articy field. Bracket-form directives (StatGate "[ZEN: 1]",
 *     ItemGate "[EMPTY_BOTTLE]") are written WITHOUT brackets in .ink
 *     source as "GATE:ZEN: 1" / "GATE:EMPTY_BOTTLE" — Ink's own
 *     choice-bracket syntax scans the whole raw line, including tags, so a
 *     literal "[...]" anywhere on a choice line corrupts it. Brackets are
 *     re-wrapped in memory in BuildNodeFromStory, never in authored content.
 *   - Skill-check choices carry a "SKILLCHECK:WORD:10" tag (same
 *     bracket-free reasoning — wrapped back into "[WORD:10]" in memory
 *     before reaching ParseSkillCheck).
 *   - menuAction handlers (enterStall / giveTabs / startGame / takeItem) are
 *     tagged "MENU:actionName" on the choice that should fire them.
 *   - Pacing is entirely hand-authored. Everything Ink has up to the next
 *     choice point prints in one go; nothing here auto-inserts a pause. To
 *     break a long stretch of prose, write a real Ink choice — `* [CONTINUE]`
 *     — at the break point. (A choice whose display text is exactly
 *     "[CONTINUE]" is flagged bIsContinuePrompt so the widget renders it as
 *     a beat rather than a line the player said.)
 *   - The ONLY button this code generates on its own is "[LEAVE]", offered
 *     when Ink dead-ends ("-> DONE" / "-> END") with no choices left, so the
 *     player always has a way out. See BuildChoicesFromStory.
 */
UCLASS()
class ECLIPSE_API UEclipseDialogueSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	bool OpenDialogue(AEclipseNpcCharacter* Npc);

	// Item-interaction dialogue — same node-view/choice flow as OpenDialogue,
	// no NPC involved. Broadcasts the same OnDialogueOpened delegate with
	// Npc=nullptr (EclipseDialogueWidget::HandleDialogueOpened already
	// null-guards every Npc dereference, so no widget changes needed).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	bool OpenItemDialogue(AEclipseItemActor* Item);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	bool MakeChoice(int32 ChoiceIndex);

	// Learn-by-doing XP granted to a stat each time the player clicks one of
	// its skill-check choices (see UEclipseGameStateSubsystem::GrantStatXP;
	// levels roll at StatXPToLevel=100). Failed attempts still teach —
	// half rate — on top of the Heat damage they already cost.
	static constexpr int32 SkillXPOnPass = 20;
	static constexpr int32 SkillXPOnFail = 10;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	void CloseDialogue();

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dialogue")
	bool IsDialogueOpen() const { return bDialogueOpen; }

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dialogue")
	const FEclipseDialogueNodeView& GetCurrentNodeView() const { return CurrentNode; }

	// The SideQuests LIST's currently-active items, as the display lines to
	// draw on the HUD checklist. Each raw item name is passed through the
	// Ink `quest_text(q)` function so the wording lives in Globals.ink with
	// the rest of the writing; an item with no case there falls through to
	// that function's `~ return q` and shows its raw name. Results are cached
	// per name, so each quest costs exactly one Ink function evaluation for
	// the whole session. Empty when the story isn't loaded.
	TArray<FString> GetActiveSideQuests();

	// Every Ink global (everything declared VAR / LIST in Globals.ink) as
	// "name = value" lines, sorted. Values go through Ink::FValue::ToString()
	// so LISTs print their live contents rather than a numeric mask. Empty
	// if the story asset failed to load. Debug-overlay only — not a save path.
	TArray<FString> GetInkVariableDump();

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueOpened       OnDialogueOpened;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueNodeChanged  OnNodeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueClosed       OnDialogueClosed;

private:
	UPROPERTY() TObjectPtr<AEclipseNpcCharacter> ActiveNpc;
	UPROPERTY() TObjectPtr<AEclipseItemActor> ActiveItem;
	UPROPERTY() TObjectPtr<UInkpotStory> Story;
	UPROPERTY() FEclipseDialogueNodeView CurrentNode;
	bool bDialogueOpen = false;
	FName CurrentDialogueId = NAME_None;

	// Parallel to CurrentNode.Choices — maps each *displayed* choice index
	// back to the real Ink choice index (hidden-gate-failed choices are
	// filtered out of what's shown, so the two can diverge). -1 marks the
	// synthetic "[Goodbye]" close-sentinel used when Ink has no more
	// content (dead end / -> END with nothing left to choose).
	TArray<int32> CurrentChoiceInkIndex;

	// Parallel to CurrentNode.Choices — the "MENU: actionName" tag captured
	// off each displayed choice at build time (NAME_None if the choice
	// carries none). Read in MakeChoice and dispatched via
	// DispatchMenuAction before the story advances past the click.
	TArray<FName> CurrentChoiceMenuAction;

	// Builds CurrentNode.Choices from Story->GetCurrentChoices() (gate-
	// evaluated, hidden failures dropped, synthetic "[LEAVE]" fallback if
	// none survive) and broadcasts OnNodeChanged.
	void BuildChoicesFromStory();

	// Skill-check parser: pulls "[WORD:10]" out of a raw string, returns
	// stat + threshold. Fed the choice's "SKILLCHECK:WORD:10" tag content,
	// re-wrapped in brackets ("[WORD:10]") in BuildNodeFromStory before
	// being passed here — see the class doc comment above for why the
	// brackets can't live in the .ink source itself.
	bool ParseSkillCheck(const FText& ChoiceText, FName& OutStat, int32& OutValue) const;

	// Ink's native "{stat > N}" per-choice conditional is evaluated inside
	// the compiled story and leaves no trace on the resulting UInkpotChoice
	// (no source text, no tag) — a choice either exists because it passed,
	// or doesn't exist at all. To label it anyway, this scans the raw .ink
	// source files (Ink/Justin/**/*.ink, project-relative) once, caching
	// every "{stat OP N} [option text]" line it finds, then looks the
	// CURRENT choice's display text up in that cache by exact match. Display
	// only — best-effort; a source file rename/move or a choice whose option
	// text contains Ink interpolation just means no label, not a crash.
	//
	// OccurrenceIndex: sibling choices can share identical button text (e.g.
	// two choices both labelled "[AESTHETICS]" with different thresholds) —
	// text alone can't tell them apart. The caller counts how many times
	// it's already seen this same display text among the choices it's built
	// THIS turn (0, 1, 2, ...) and passes that in; this returns the Nth cache
	// entry with matching text, in the same top-to-bottom source order the
	// choices were authored in, so sibling duplicates resolve correctly.
	bool FindInkGateLabel(const FString& ChoiceDisplayText, int32 OccurrenceIndex, FName& OutStat, int32& OutValue, bool& OutIsPass) const;

	struct FInkGateLabel { FString ChoiceText; FName Stat; int32 Value; bool bIsPass; };
	mutable TArray<FInkGateLabel> InkGateLabelCache;
	mutable bool bInkGateLabelCacheBuilt = false;

	// Ink list-item name → display line, filled on demand by
	// GetActiveSideQuests so quest_text() is evaluated once per quest.
	TMap<FString, FString> SideQuestTextCache;
	void BuildInkGateLabelCache() const;

	// Stage-directions parser. Accepts a comma-separated list of tokens like
	// "[ZEN: 1], [BAGGIE], +1 RHYTHM, -2 ENERGY" and emits a directive
	// array. Tokens that don't match any kind are skipped silently (with a
	// Log warning so authors notice typos). Fed the current line's/choice's
	// Ink tags (joined with commas) — same grammar as when this read from
	// an Articy stage-directions field.
	static TArray<FEclipseStageDirective> ParseStageDirections(const FString& Raw);

	// Apply a single effect-kind directive to the player's state. StatEffect
	// keys route through UEclipseGameStateSubsystem::ApplyStatDelta;
	// MeterEffect keys route through ChangeMeter (which clamps to [0,10]
	// and fires OnPlayerDeath on a Heat→0 transition). No-op for
	// non-effect kinds (defensive).
	void ApplyStageEffect(const FEclipseStageDirective& Eff) const;

	// Evaluate every gate directive on a choice against current state.
	// Sets bAvailable=false and a "(need …)" GateHint when any gate fails.
	// Existing skill-check gates set earlier in the choice-build path are
	// preserved; this only ADDs availability constraints.
	void EvaluateChoiceGates(FEclipseDialogueChoice& Choice) const;

	// Render the orange "−1 AESTHETICS · +2 ENERGY" summary line from a
	// node's effect directives. Returns empty text when none apply.
	static FText BuildEffectsLineText(const TArray<FEclipseStageDirective>& Directives);

	// menuAction dispatcher (enterStall / giveTabs / startGame / takeItem)
	void DispatchMenuAction(FName ActionName);

	// Populate CurrentNode (+ CurrentChoiceInkIndex) from wherever the Ink
	// story currently sits — pulls the full body via ContinueMaximally(),
	// tags via GetCurrentTags() (→ EffectsLine, display only, same as
	// before), and choices via GetCurrentChoices() (gate-evaluated, hidden
	// failures dropped, synthetic "[Goodbye]" fallback if none survive or
	// Ink has hit a dead end). Broadcasts OnNodeChanged.
	//
	// EchoedChoiceText: when set (MakeChoice passes the just-picked choice's
	// label), a leading line of the continued body that exactly matches it
	// is stripped before it becomes CurrentNode.Body. Ink's own rule for a
	// choice with no "[...]" is to print the choice text once more as the
	// following line of story content — by design, so authors can write
	// "* Do you know how I can get in?" without brackets and have it read
	// naturally in the transcript. The dialogue widget already renders that
	// same text as the player's "YOU" line the instant the choice is
	// clicked, so without this strip it would print a second time here,
	// attributed to the NPC (CurrentNode.SpeakerName is always the NPC).
	void BuildNodeFromStory(const FText* EchoedChoiceText = nullptr);

	// "enterStall" implementation — closes the AngelSeeker dialogue and tells
	// her to step aside, clearing the doorway. The Angel itself is a normal
	// level-placed NPC the player walks up to and talks to via [E]; nothing
	// auto-opens here.
	void EnterStallTransition();
};
