// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseDialogueSubsystem.generated.h"

class AEclipseNpcCharacter;

// ── Stage directives (Articy "Stage directions" field) ──────────────────────
// Parsed from a comma-separated string of tokens authored on each Articy
// DialogueFragment. Four kinds:
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
	//   MeterEffect / MeterCompareGate       → "heat" / "thirst" / "stimulation"
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
	// Stimulation cost when this choice is a failed skill check that the
	// player clicks anyway. Surfaced to the widget so it can render a
	// "[-N STIMULATION]" risk hint, consumed in MakeChoice via the
	// GameStateSubsystem ChangeStimulation API. (Renamed from
	// EnergyDamageOnFail — Energy meter was absorbed into Stimulation.)
	UPROPERTY(BlueprintReadOnly) int32 StimulationDamageOnFail = 2;

	// All stage directives parsed from this choice's Articy StageDirections
	// string. Gates are evaluated when the choice is built (sets bAvailable +
	// GateHint); effects fire in MakeChoice when the player clicks.
	UPROPERTY(BlueprintReadOnly) TArray<FEclipseStageDirective> StageDirectives;

	// Human-readable hint about WHY this choice is unavailable, generated
	// from any gate directive that failed. Empty when available.
	// Examples: "(need ZEN 1)", "(no BAGGIE)".
	UPROPERTY(BlueprintReadOnly) FText GateHint;
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
 * Articy runtime wrapper. Mirrors the JS articyDB + getEntryNode +
 * renderArticyNode flow (index.html ~lines 2500–2900 + 3700+).
 *
 * Responsibilities:
 *   - Load the imported UArticyDatabase (see ArticyImporter plugin) on Init
 *   - Map dialogueId → entry node, drive a single active conversation
 *   - Evaluate skill check choice texts ("[WORD:10] ...") against
 *     UEclipseGameStateSubsystem
 *   - Fire menuAction handlers: enterStall / giveTabs / startGame
 *
 * Slice-scope dialogue IDs:
 *   - 0x0100000000000F00  Dlg_AngelSeeker (synthetic, runtime-injected for now)
 *   - 0x0100000000000B00  Dlg_StallVoice  (also synthetic)
 *   - 0x0100000000000C00  Dlg_StallVoice2
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

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	bool MakeChoice(int32 ChoiceIndex);

	// Learn-by-doing XP granted to a stat each time the player clicks one of
	// its skill-check choices (see UEclipseGameStateSubsystem::GrantStatXP;
	// levels roll at StatXPToLevel=100). Failed attempts still teach —
	// half rate — on top of the Stimulation damage they already cost.
	static constexpr int32 SkillXPOnPass = 20;
	static constexpr int32 SkillXPOnFail = 10;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Dialogue")
	void CloseDialogue();

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dialogue")
	bool IsDialogueOpen() const { return bDialogueOpen; }

	UFUNCTION(BlueprintPure, Category = "Eclipse|Dialogue")
	const FEclipseDialogueNodeView& GetCurrentNodeView() const { return CurrentNode; }

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueOpened       OnDialogueOpened;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueNodeChanged  OnNodeChanged;

	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Dialogue")
	FEclipseDialogueClosed       OnDialogueClosed;

	/**
	 * Synthetic dialogue injection. The JS prototype injects three trees at runtime
	 * (Intro / AngelSeeker / Angel) that aren't yet authored in Articy. This subsystem
	 * provides the same fallback so the slice doesn't depend on Articy authoring
	 * being complete.
	 *
	 * TODO(post-slice): author these in Articy directly and remove the runtime
	 * injection. See PORT_PLAN.md §5.
	 */
	void InjectSyntheticDialogues();

private:
	UPROPERTY() TObjectPtr<AEclipseNpcCharacter> ActiveNpc;
	UPROPERTY() FEclipseDialogueNodeView CurrentNode;
	bool bDialogueOpen = false;
	FName CurrentDialogueId = NAME_None;
	FName CurrentNodeId = NAME_None;

	// Skill-check parser: pulls "[WORD:10]" from choice text, returns stat + threshold.
	bool ParseSkillCheck(const FText& ChoiceText, FName& OutStat, int32& OutValue) const;

	// Articy "Stage directions" parser. Accepts a comma-separated list of
	// tokens like "[ZEN: 1], [BAGGIE], +1 RHYTHM, -2 ENERGY" and emits a
	// directive array. Tokens that don't match any kind are skipped silently
	// (with a Log warning so authors notice typos).
	static TArray<FEclipseStageDirective> ParseStageDirections(const FString& Raw);

	// Apply a single effect-kind directive to the player's state. StatEffect
	// keys route through UEclipseGameStateSubsystem::ApplyStatDelta;
	// MeterEffect keys route through ChangeMeter (which clamps to [0,10]
	// and fires OnPlayerDeath on a Stimulation→0 transition). No-op for
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

	// ── Articy runtime fallback (reads from UArticyDatabase) ────────────────
	//
	// When the synthetic node store doesn't have a NodeId, fall through to
	// the Articy database that's populated by ArticyImporter.Reimport. Reads
	// Description / MenuText / StageDirections / OutputPins via the generic
	// IArticy* interfaces — no project-specific generated types referenced
	// here, so this code compiles whether or not the importer has run.
	//
	// Returns true if NodeId resolved against the Articy db and OutNode +
	// OutNextChoiceIds were filled. Returns false when db is empty or NodeId
	// isn't a known Articy object — caller continues to its existing logic.
	bool ResolveArticyNode(FName NodeId,
		FEclipseDialogueNodeView& OutNode,
		TArray<FName>& OutNextChoiceIds) const;

	// Given an Articy dialogue object (e.g. "Dlg_97F8ED64"), follow its
	// first output-pin connection to the first DialogueFragment that owns a
	// MenuText/Text — that's the "entry" the player sees on conversation open.
	// Returns NAME_None on miss.
	FName ResolveArticyDialogueEntry(FName DialogueId) const;

	// menuAction dispatcher (enterStall / giveTabs / startGame / etc.)
	void DispatchMenuAction(FName ActionName);

	// Populate CurrentNode from a synthetic node ID, advancing through any
	// player-choice fragments to the next NPC speech fragment.
	void AdvanceToNode(FName NodeId);

	// "enterStall" implementation — closes the AngelSeeker dialogue and tells
	// her to step aside, clearing the doorway. The Angel itself is a normal
	// level-placed NPC the player walks up to and talks to via [E]; nothing
	// auto-opens here.
	void EnterStallTransition();
};
