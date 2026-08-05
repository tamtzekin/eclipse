// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDialogueSubsystem.h"
#include "Eclipse.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Items/EclipseItemActor.h"
#include "Player/EclipsePlayerCharacter.h"
#include "EclipseGameStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "TimerManager.h"

// ── Inkpot (Ink) runtime ──
#include "Inkpot/Inkpot.h"
#include "Inkpot/InkpotStory.h"
#include "Inkpot/InkpotChoice.h"
#include "Asset/InkpotStoryAsset.h"

namespace
{
	const TCHAR* MainStoryAssetPath = TEXT("/Game/Justin/Dialogue/DA_MainStory.DA_MainStory");
}

// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("DialogueSubsystem::Initialize"));

	if (UInkpotStoryAsset* Asset = LoadObject<UInkpotStoryAsset>(nullptr, MainStoryAssetPath))
	{
		if (UInkpot* InkpotSubsystem = GEngine ? GEngine->GetEngineSubsystem<UInkpot>() : nullptr)
		{
			Story = InkpotSubsystem->BeginStory(Asset);
		}
	}
	if (!Story)
	{
		UE_LOG(LogEclipse, Error, TEXT("DialogueSubsystem: failed to load/begin main Ink story at '%s'"), MainStoryAssetPath);
	}
}

void UEclipseDialogueSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("DialogueSubsystem::Deinitialize"));
	Super::Deinitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenDialogue — syncs quest-state into the Ink story, then jumps to the
// NPC's knot (DialogueId). Quest-state branching that used to be 8
// hardcoded C++ entry-node lookups now lives in AngelSeeker.ink itself,
// driven by these synced variables (see Globals.ink).
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseDialogueSubsystem::OpenDialogue(AEclipseNpcCharacter* Npc)
{
	if (!Npc || Npc->DialogueId == NAME_None || !Story)
	{
		UE_LOG(LogEclipse, Warning, TEXT("OpenDialogue: missing NPC, dialogueId, or Ink story"));
		return false;
	}

	ActiveNpc = Npc;
	ActiveItem = nullptr;
	CurrentDialogueId = Npc->DialogueId;
	bDialogueOpen = true;

	// Pause the chapter clock while dialogue is open — TickChapterClock
	// early-returns on !bClockRunning so no game-time elapses during
	// conversation. CloseDialogue() flips it back on. Each MakeChoice call
	// will still add +1 game-second so reading through a long branch costs
	// time even though the wall clock is frozen.
	UEclipseGameStateSubsystem* State = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		State = GI->GetSubsystem<UEclipseGameStateSubsystem>();
	}
	if (State)
	{
		State->SetClockRunning(false);
	}

	// Sync GameState → Ink globals (read-only from Ink's perspective; all
	// state *writes* still flow through the tag → ApplyStageEffect path).
	{
		bool bOk = false;
		Story->SetBool(TEXT("has_hair"), State && State->Quest.bHasHair, bOk);
		Story->SetBool(TEXT("has_eye"), State && State->Quest.bHasEye, bOk);
		Story->SetString(TEXT("quest_stage"), State ? State->Quest.Stage.ToString() : TEXT("intro"), bOk);
		Story->SetBool(TEXT("met_npc"), State && State->HasMetNPC(Npc->NpcName), bOk);
	}

	if (Npc->bIsAngelSeeker && State)
	{
		State->RecordMetNPC(Npc->NpcName, CurrentDialogueId);
	}

	Story->ChoosePath(CurrentDialogueId.ToString());
	BuildNodeFromStory();

	// ── OTS rotation: face the NPC (matches JS prototype OTS transition) ──
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (AEclipsePlayerCharacter* Player = Cast<AEclipsePlayerCharacter>(PC->GetPawn()))
			{
				Player->StartFaceTarget(Npc->GetActorLocation());
				Npc->StartFacePlayer(Player);
			}
		}
	}

	OnDialogueOpened.Broadcast(Npc);
	// (Don't re-broadcast OnNodeChanged here — BuildNodeFromStory above
	// already fired it. Re-broadcasting causes the dialogue widget's
	// body-cascade animation to restart mid-flight every time a
	// conversation opens, visible as a brief judder + a doubled
	// "StartBodyAnimation" log pair.)
	UE_LOG(LogEclipse, Log, TEXT("Opened dialogue '%s' with NPC '%s'"),
		*CurrentDialogueId.ToString(), *Npc->NpcName.ToString());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenItemDialogue — same flow as OpenDialogue, driven by an item's
// DialogueId instead of an NPC's. No quest-state sync (items don't branch
// on it today). Broadcasts OnDialogueOpened with Npc=nullptr.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseDialogueSubsystem::OpenItemDialogue(AEclipseItemActor* Item)
{
	if (!Item || Item->DialogueId == NAME_None || !Story)
	{
		UE_LOG(LogEclipse, Warning, TEXT("OpenItemDialogue: missing item, dialogueId, or Ink story"));
		return false;
	}

	ActiveNpc = nullptr;
	ActiveItem = Item;
	CurrentDialogueId = Item->DialogueId;
	bDialogueOpen = true;

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->SetClockRunning(false);
		}
	}

	Story->ChoosePath(CurrentDialogueId.ToString());
	BuildNodeFromStory();

	OnDialogueOpened.Broadcast(nullptr);
	UE_LOG(LogEclipse, Log, TEXT("Opened item dialogue '%s'"), *CurrentDialogueId.ToString());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeChoice — advance the dialogue tree
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseDialogueSubsystem::MakeChoice(int32 ChoiceIndex)
{
	if (!bDialogueOpen) return false;
	if (!CurrentNode.Choices.IsValidIndex(ChoiceIndex)) return false;

	const FEclipseDialogueChoice& Chosen = CurrentNode.Choices[ChoiceIndex];
	UE_LOG(LogEclipse, Log, TEXT("Choice [%d]: %s"), ChoiceIndex, *Chosen.Text.ToString());

	// Apply stage-directive effects from this choice. Effects fire on click
	// for every choice (available OR gated — gated choices still consume
	// their cost so designers can author "you tried and failed" branches
	// that pay a stat tax). Gates that disabled the choice in the widget
	// mean the button is greyed out and click never reaches here anyway.
	for (const FEclipseStageDirective& D : Chosen.StageDirectives)
	{
		const bool bIsEffect =
			D.Kind == EEclipseStageDirectiveKind::StatEffect ||
			D.Kind == EEclipseStageDirectiveKind::MeterEffect ||
			D.Kind == EEclipseStageDirectiveKind::HiddenStatEffect;
		if (bIsEffect)
		{
			ApplyStageEffect(D);
		}
	}

	// Skill-check resolution — every clicked check grinds its stat
	// (learn-by-doing: use a skill in conversation, earn XP toward its next
	// level). Passing grants the full SkillXPOnPass; a failed attempt still
	// teaches at half rate but also pays the Stimulation failure tax. The
	// dialogue widget no longer disables failed-skill buttons; players can
	// attempt risky checks at a cost.
	if (Chosen.bIsSkillCheck)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				if (Chosen.bAvailable)
				{
					GS->GrantStatXP(Chosen.SkillCheckStat, SkillXPOnPass);
				}
				else
				{
					GS->GrantStatXP(Chosen.SkillCheckStat, SkillXPOnFail);
					if (Chosen.StimulationDamageOnFail > 0)
					{
						UE_LOG(LogEclipse, Log, TEXT("Choice: failed skill check '%s' (need %d, got %d) -> -%d Stimulation"),
							*Chosen.SkillCheckStat.ToString(), Chosen.SkillCheckValue,
							GS->GetStatValue(Chosen.SkillCheckStat), Chosen.StimulationDamageOnFail);
						// "Damage" = push toward 0 (the death extreme).
						GS->ChangeStimulation(-Chosen.StimulationDamageOnFail);
					}
				}
			}
		}
	}

	// Helper — bump the chapter clock by 20 game-seconds on continuing
	// choices. Exit choices that fall through to CloseDialogue pay no
	// cost — the live clock just resumes. 20s (not 30) keeps the visible
	// minute readout from ticking cleanly on every 2nd choice — the
	// irregular crossings feel less mechanical.
	//
	// NOTE: thirst no longer auto-drains per choice. The new sweet-spot
	// meter model is event-driven; thirst only moves when an explicit
	// stage directive (or item use) pushes it. Designer can add a
	// "+1 THIRST" Ink tag to a choice for a per-choice dehydration tax if
	// they want one.
	auto BumpClockForContinue = [this]()
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				const float Before = GS->ChapterElapsedSeconds;
				GS->ChapterElapsedSeconds += 20.0f;
				UE_LOG(LogEclipse, Log, TEXT("Dlg: choice +20s  %.1f -> %.1f"),
					Before, GS->ChapterElapsedSeconds);
			}
		}
	};

	const int32 InkIndex = CurrentChoiceInkIndex.IsValidIndex(ChoiceIndex)
		? CurrentChoiceInkIndex[ChoiceIndex] : -1;

	// Dispatch this choice's menu action (if any) BEFORE advancing the
	// story — matches the pre-Ink ordering (the action fires tied to the
	// click that carries it). Actions like "startGame"/"takeItem" close the
	// dialogue themselves; when they do, there's nothing left to advance.
	const FName MenuAction = CurrentChoiceMenuAction.IsValidIndex(ChoiceIndex)
		? CurrentChoiceMenuAction[ChoiceIndex] : NAME_None;
	if (MenuAction != NAME_None)
	{
		DispatchMenuAction(MenuAction);
		if (!bDialogueOpen) return true;
	}

	// InkIndex < 0 is the synthetic "[Goodbye]" sentinel (Ink had no more
	// content at this decision point) — nothing to choose, just close.
	// No +20s — the live clock just resumes.
	if (InkIndex < 0)
	{
		CloseDialogue();
		return true;
	}

	BumpClockForContinue();
	Story->ChooseChoiceIndex(InkIndex);
	BuildNodeFromStory();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildNodeFromStory — populate CurrentNode (+ CurrentChoiceInkIndex /
// CurrentChoiceMenuAction) from wherever the Ink story currently sits.
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::BuildNodeFromStory()
{
	CurrentNode = FEclipseDialogueNodeView{};
	CurrentChoiceInkIndex.Reset();
	CurrentChoiceMenuAction.Reset();

	if (!Story)
	{
		CloseDialogue();
		return;
	}

	CurrentNode.SpeakerName = ActiveNpc ? ActiveNpc->NpcName
		: (ActiveItem ? ActiveItem->ItemId : FName(TEXT("NPC")));

	const FString Body = Story->ContinueMaximally();
	CurrentNode.Body = FText::FromString(Body.TrimStartAndEnd());

	// Node-level tags → EffectsLine (display only — a preview, not
	// auto-applied; only choice-level directives get applied on click, in
	// MakeChoice).
	{
		const FString NodeTags = FString::Join(Story->GetCurrentTags(), TEXT(","));
		CurrentNode.EffectsLine = BuildEffectsLineText(ParseStageDirections(NodeTags));
	}

	UEclipseGameStateSubsystem* State = nullptr;
	if (UWorld* W = GetWorld())
		if (UGameInstance* GI = W->GetGameInstance())
			State = GI->GetSubsystem<UEclipseGameStateSubsystem>();

	const TArray<UInkpotChoice*>& InkChoices = Story->GetCurrentChoices();
	for (int32 i = 0; i < InkChoices.Num(); ++i)
	{
		UInkpotChoice* InkChoice = InkChoices[i];
		if (!InkChoice) continue;

		FEclipseDialogueChoice Choice;
		Choice.Text = InkChoice->GetText();

		// Split this choice's tags into: "MENU:x" (menu-action dispatch),
		// "SKILLCHECK:STAT:N" (skill-check marker), "GATE:..." (a StatGate/
		// ItemGate directive — re-wrapped in brackets below before handing
		// to ParseStageDirections/ParseSkillCheck), and everything else
		// (effect/compare-gate tokens, which contain no brackets and pass
		// through as-is).
		//
		// None of these tags may contain a literal '[' or ']' in the raw
		// .ink source: Ink's own choice-bracket syntax ("* text[list-only]
		// output-only") scans the WHOLE raw choice line for the first
		// "[...]" pair — including anything that will become a tag — and
		// splices it into the choice's display text, corrupting it. Brackets
		// only get reintroduced here, in memory, never in authored content.
		FName MenuAction = NAME_None;
		FString SkillCheckTag;
		TArray<FString> DirectiveTokens;
		for (const FString& Tag : InkChoice->GetTags())
		{
			if (Tag.StartsWith(TEXT("MENU:")))
			{
				MenuAction = FName(*Tag.Mid(5).TrimStartAndEnd());
			}
			else if (Tag.StartsWith(TEXT("SKILLCHECK:")))
			{
				SkillCheckTag = FString::Printf(TEXT("[%s]"), *Tag.Mid(11).TrimStartAndEnd());
			}
			else if (Tag.StartsWith(TEXT("GATE:")))
			{
				DirectiveTokens.Add(FString::Printf(TEXT("[%s]"), *Tag.Mid(5).TrimStartAndEnd()));
			}
			else
			{
				DirectiveTokens.Add(Tag);
			}
		}

		if (!SkillCheckTag.IsEmpty())
		{
			FName Stat; int32 Value = 0;
			if (ParseSkillCheck(FText::FromString(SkillCheckTag), Stat, Value))
			{
				Choice.bIsSkillCheck = true;
				Choice.SkillCheckStat = Stat;
				Choice.SkillCheckValue = Value;
				if (State)
				{
					Choice.bAvailable = State->GetStatValue(Stat) >= Value;
				}
			}
		}

		// Stage directives: parse + attach + evaluate gates. Same grammar as
		// when this read from an Articy stage-directions field — just fed
		// the choice's own Ink tags (minus the MENU:/SKILLCHECK: ones
		// already pulled out above) instead.
		// EvaluateChoiceGates only flips bAvailable to FALSE; it never
		// re-enables a choice the skill check above already disqualified.
		// GateHint is populated with the first failing gate's "(need …)".
		Choice.StageDirectives = ParseStageDirections(FString::Join(DirectiveTokens, TEXT(",")));
		EvaluateChoiceGates(Choice);

		// Hidden-gate failures don't exist for this player — drop them
		// entirely (no greyed row, no hint).
		if (Choice.bHiddenGateFailed) continue;

		CurrentNode.Choices.Add(Choice);
		CurrentChoiceInkIndex.Add(i);
		CurrentChoiceMenuAction.Add(MenuAction);
	}

	// Always offer at least one way out — a dead end (Ink hit -> END with
	// no choices) or every choice getting hidden-gate-filtered above.
	if (CurrentNode.Choices.IsEmpty())
	{
		FEclipseDialogueChoice Goodbye;
		Goodbye.Text = FText::FromString(TEXT("[Goodbye]"));
		Goodbye.bAvailable = true;
		CurrentNode.Choices.Add(Goodbye);
		CurrentChoiceInkIndex.Add(-1);
		CurrentChoiceMenuAction.Add(NAME_None);
	}

	OnNodeChanged.Broadcast(CurrentNode);
}

// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	AEclipseNpcCharacter* NpcToRelease = ActiveNpc;   // ActiveNpc gets nulled below
	ActiveNpc = nullptr;
	ActiveItem = nullptr;
	CurrentDialogueId = NAME_None;
	CurrentNode = FEclipseDialogueNodeView{};
	CurrentChoiceInkIndex.Reset();
	CurrentChoiceMenuAction.Reset();

	// Resume the chapter clock that OpenDialogue paused.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->SetClockRunning(true);
		}
	}

	// Release the OTS rotation lock on the player
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (AEclipsePlayerCharacter* Player = Cast<AEclipsePlayerCharacter>(PC->GetPawn()))
			{
				Player->StopFaceTarget();
			}
			if (NpcToRelease)
			{
				NpcToRelease->StopFacePlayer();
			}
		}
	}

	OnDialogueClosed.Broadcast();
	UE_LOG(LogEclipse, Log, TEXT("Dialogue closed"));
}

bool UEclipseDialogueSubsystem::ParseSkillCheck(const FText& ChoiceText, FName& OutStat, int32& OutValue) const
{
	const FString S = ChoiceText.ToString();
	int32 OpenBracket = INDEX_NONE; S.FindChar('[', OpenBracket);
	int32 CloseBracket = INDEX_NONE; S.FindChar(']', CloseBracket);
	if (OpenBracket == INDEX_NONE || CloseBracket == INDEX_NONE || CloseBracket <= OpenBracket + 2) return false;

	const FString Inner = S.Mid(OpenBracket + 1, CloseBracket - OpenBracket - 1);
	int32 Colon = INDEX_NONE; Inner.FindChar(':', Colon);
	if (Colon == INDEX_NONE) return false;

	OutStat = FName(*Inner.Left(Colon).ToLower());
	OutValue = FCString::Atoi(*Inner.Mid(Colon + 1));
	return OutStat == TEXT("aesthetics")
		|| OutStat == TEXT("stimulation")
		|| OutStat == TEXT("rhythm")
		|| OutStat == TEXT("zen")
		|| OutStat == TEXT("psychedelics");
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stage-directions plumbing (parser + eval + apply + display-string builder)
//
//  Grammar (comma-separated tokens, whitespace flexible):
//    [STAT_NAME: N]      StatGate          — gates availability
//    [ITEM_NAME]         ItemGate          — gates availability
//    +N STAT_NAME        StatEffect        — applied on click
//    -N STAT_NAME        StatEffect        — applied on click
//    +N METER_NAME       MeterEffect       — applied on click
//    -N METER_NAME       MeterEffect       — applied on click
//    METER_NAME OP N     MeterCompareGate  — gates availability
//
//  STAT_NAME  ∈ { AESTHETICS, RHYTHM, ZEN, PSYCHEDELICS }
//  METER_NAME ∈ { HEAT, THIRST, STIMULATION }
//  OP         ∈ { <, <=, ==, !=, >=, > }
//  ITEM_NAME  = DT_Items row id in ALL CAPS (matched case-insensitively
//               against the lowercased inventory ids).
//
//  Examples:
//    "HEAT > 8"             — only available when overheated
//    "STIMULATION < 3"      — only available when fatigued
//    "+1 HEAT"              — warms you up one notch on click
//    "[ZEN: 3], -1 THIRST"  — needs Zen ≥ 3 AND drinks one notch of thirst
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Recognise the four gameplay stats. (Stimulation moved into the meter
	// system — see IsKnownMeterKey below.)
	bool IsKnownStatKey(FName Lower)
	{
		return Lower == TEXT("aesthetics")
			|| Lower == TEXT("rhythm")
			|| Lower == TEXT("zen")
			|| Lower == TEXT("psychedelics");
	}

	bool IsKnownMeterKey(FName Lower)
	{
		return Lower == TEXT("heat")
			|| Lower == TEXT("thirst")
			|| Lower == TEXT("stimulation");
	}

	// Hidden numeric social stats (currently just Annoyance). Numeric like
	// meters — gated by compare ops, moved by signed effects.
	bool IsKnownHiddenStatKey(FName Lower)
	{
		return Lower == TEXT("annoyance");
	}

	// Hidden identity tags. Compared by name equality (== / !=) only.
	bool IsKnownIdentityKey(FName Lower)
	{
		return Lower == TEXT("gender")
			|| Lower == TEXT("race");
	}

	// Look for any of "<=", ">=", "==", "!=", "<", ">" inside Tok. Returns
	// the byte index + sets OutOp + sets OutOpLen (1 or 2 chars). Returns
	// INDEX_NONE if no operator is present.
	int32 FindCompareOp(const FString& Tok, EEclipseCompareOp& OutOp, int32& OutOpLen)
	{
		// Two-char operators take precedence so "<=" doesn't get parsed as "<".
		struct { const TCHAR* Sym; EEclipseCompareOp Op; int32 Len; } Ops[] = {
			{ TEXT("<="), EEclipseCompareOp::LessEqual,    2 },
			{ TEXT(">="), EEclipseCompareOp::GreaterEqual, 2 },
			{ TEXT("=="), EEclipseCompareOp::Equal,        2 },
			{ TEXT("!="), EEclipseCompareOp::NotEqual,     2 },
			{ TEXT("<"),  EEclipseCompareOp::Less,         1 },
			{ TEXT(">"),  EEclipseCompareOp::Greater,      1 },
		};
		for (const auto& Entry : Ops)
		{
			const int32 Idx = Tok.Find(Entry.Sym);
			if (Idx != INDEX_NONE)
			{
				OutOp = Entry.Op;
				OutOpLen = Entry.Len;
				return Idx;
			}
		}
		return INDEX_NONE;
	}
}

TArray<FEclipseStageDirective> UEclipseDialogueSubsystem::ParseStageDirections(const FString& Raw)
{
	TArray<FEclipseStageDirective> Out;
	if (Raw.IsEmpty()) return Out;

	TArray<FString> Tokens;
	Raw.ParseIntoArray(Tokens, TEXT(","), /*bCullEmpty=*/true);
	for (FString Tok : Tokens)
	{
		Tok.TrimStartAndEndInline();
		if (Tok.IsEmpty()) continue;

		// Gate forms start with '['.
		if (Tok.StartsWith(TEXT("[")) && Tok.EndsWith(TEXT("]")))
		{
			FString Inner = Tok.Mid(1, Tok.Len() - 2).TrimStartAndEnd();
			int32 Colon = INDEX_NONE; Inner.FindChar(':', Colon);
			if (Colon != INDEX_NONE)
			{
				// [STAT: N] — stat gate.
				FString StatStr  = Inner.Left(Colon).TrimStartAndEnd().ToLower();
				FString ValStr   = Inner.Mid(Colon + 1).TrimStartAndEnd();
				const FName Stat(*StatStr);
				if (IsKnownStatKey(Stat))
				{
					FEclipseStageDirective D;
					D.Kind = EEclipseStageDirectiveKind::StatGate;
					D.Stat = Stat;
					D.Value = FCString::Atoi(*ValStr);
					Out.Add(D);
					continue;
				}
				UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: unknown stat in gate token '%s'"), *Tok);
				continue;
			}
			// [ITEM_NAME] — item gate. Lowercase the inner for matching.
			FEclipseStageDirective D;
			D.Kind = EEclipseStageDirectiveKind::ItemGate;
			D.ItemId = FName(*Inner.ToLower());
			Out.Add(D);
			continue;
		}

		// Effect forms start with '+' or '-' (signed integer, then a STAT
		// or METER name).
		if (Tok.StartsWith(TEXT("+")) || Tok.StartsWith(TEXT("-")))
		{
			// Pull the leading signed-integer.
			int32 i = 1;   // skip sign
			while (i < Tok.Len() && FChar::IsDigit(Tok[i])) ++i;
			if (i <= 1)
			{
				UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: no number after sign in '%s'"), *Tok);
				continue;
			}
			const int32 Delta = FCString::Atoi(*Tok.Left(i));
			FString Rest = Tok.Mid(i).TrimStartAndEnd().ToLower();
			if (Rest.IsEmpty())
			{
				UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: missing target in '%s'"), *Tok);
				continue;
			}
			const FName Target(*Rest);
			FEclipseStageDirective D;
			if (IsKnownMeterKey(Target))
			{
				D.Kind = EEclipseStageDirectiveKind::MeterEffect;
				D.Stat = Target;
			}
			else if (IsKnownStatKey(Target))
			{
				D.Kind = EEclipseStageDirectiveKind::StatEffect;
				D.Stat = Target;
			}
			else if (IsKnownHiddenStatKey(Target))
			{
				D.Kind = EEclipseStageDirectiveKind::HiddenStatEffect;
				D.Stat = Target;
			}
			else
			{
				UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: unknown effect target '%s' in '%s'"),
					*Rest, *Tok);
				continue;
			}
			D.Value = Delta;
			Out.Add(D);
			continue;
		}

		// Comparison gate: "<METER> <OP> <N>" e.g. "HEAT > 8".
		{
			EEclipseCompareOp Op = EEclipseCompareOp::GreaterEqual;
			int32 OpLen = 0;
			const int32 OpIdx = FindCompareOp(Tok, Op, OpLen);
			if (OpIdx != INDEX_NONE)
			{
				FString Left  = Tok.Left(OpIdx).TrimStartAndEnd().ToLower();
				FString Right = Tok.Mid(OpIdx + OpLen).TrimStartAndEnd();
				const FName Key(*Left);
				if (IsKnownMeterKey(Key))
				{
					FEclipseStageDirective D;
					D.Kind  = EEclipseStageDirectiveKind::MeterCompareGate;
					D.Stat  = Key;
					D.Value = FCString::Atoi(*Right);
					D.Op    = Op;
					Out.Add(D);
					continue;
				}
				if (IsKnownHiddenStatKey(Key))
				{
					// Numeric compare on a hidden social stat, e.g.
					// "ANNOYANCE >= 4".
					FEclipseStageDirective D;
					D.Kind  = EEclipseStageDirectiveKind::HiddenStatGate;
					D.Stat  = Key;
					D.Value = FCString::Atoi(*Right);
					D.Op    = Op;
					Out.Add(D);
					continue;
				}
				if (IsKnownStatKey(Key))
				{
					// Silent unlock on a visible stat, e.g. "ZEN >= 3" —
					// the choice is dropped entirely until the level is
					// reached (vs "[ZEN: 3]" StatGate which greys + hints).
					FEclipseStageDirective D;
					D.Kind  = EEclipseStageDirectiveKind::StatCompareGate;
					D.Stat  = Key;
					D.Value = FCString::Atoi(*Right);
					D.Op    = Op;
					Out.Add(D);
					continue;
				}
				if (IsKnownIdentityKey(Key))
				{
					// Name equality on a hidden identity tag, e.g.
					// "GENDER == female". Only == / != make sense.
					if (Op != EEclipseCompareOp::Equal && Op != EEclipseCompareOp::NotEqual)
					{
						UE_LOG(LogEclipse, Warning,
							TEXT("ParseStageDirections: identity gate '%s' needs == or != (got other op) — ignored"),
							*Tok);
						continue;
					}
					FEclipseStageDirective D;
					D.Kind   = EEclipseStageDirectiveKind::IdentityGate;
					D.Stat   = Key;                          // "gender" / "race"
					D.ItemId = FName(*Right.ToLower());      // RHS value, e.g. "female"
					D.Op     = Op;
					Out.Add(D);
					continue;
				}
				UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: unknown key in compare gate '%s'"), *Tok);
				continue;
			}
		}

		UE_LOG(LogEclipse, Warning, TEXT("ParseStageDirections: unrecognised token '%s'"), *Tok);
	}
	return Out;
}

void UEclipseDialogueSubsystem::ApplyStageEffect(const FEclipseStageDirective& Eff) const
{
	UEclipseGameStateSubsystem* State = nullptr;
	if (UWorld* W = GetWorld())
		if (UGameInstance* GI = W->GetGameInstance())
			State = GI->GetSubsystem<UEclipseGameStateSubsystem>();
	if (!State) return;

	switch (Eff.Kind)
	{
	case EEclipseStageDirectiveKind::StatEffect:
		State->ApplyStatDelta(Eff.Stat, Eff.Value);
		break;
	case EEclipseStageDirectiveKind::MeterEffect:
		// Signed delta on the 0..10 meter scale, clamped + broadcast inside
		// ChangeMeter. Stimulation==0 also fires OnPlayerDeath from there.
		State->ChangeMeter(Eff.Stat, Eff.Value);
		break;
	case EEclipseStageDirectiveKind::HiddenStatEffect:
		// Signed delta on a hidden social stat (Annoyance), clamped to
		// [0, AnnoyanceMax] inside ChangeHiddenStat.
		State->ChangeHiddenStat(Eff.Stat, Eff.Value);
		break;
	default:
		// Gates aren't effects — silently ignore.
		break;
	}
}

void UEclipseDialogueSubsystem::EvaluateChoiceGates(FEclipseDialogueChoice& Choice) const
{
	UEclipseGameStateSubsystem* State = nullptr;
	if (UWorld* W = GetWorld())
		if (UGameInstance* GI = W->GetGameInstance())
			State = GI->GetSubsystem<UEclipseGameStateSubsystem>();
	if (!State) return;

	for (const FEclipseStageDirective& D : Choice.StageDirectives)
	{
		if (D.Kind == EEclipseStageDirectiveKind::StatGate)
		{
			const int32 Cur = State->GetStatValue(D.Stat);
			if (Cur < D.Value)
			{
				Choice.bAvailable = false;
				if (Choice.GateHint.IsEmpty())
				{
					Choice.GateHint = FText::FromString(FString::Printf(
						TEXT("(need %s %d)"), *D.Stat.ToString().ToUpper(), D.Value));
				}
			}
		}
		else if (D.Kind == EEclipseStageDirectiveKind::ItemGate)
		{
			// Inventory entries are runtime ids ("<base>__<actor>"); compare
			// the base form against the gate's lowercased item id.
			const bool bHave = State->Inventory.ContainsByPredicate(
				[&](const FName& Id){ return UEclipseGameStateSubsystem::GetBaseItemId(Id) == D.ItemId; });
			if (!bHave)
			{
				Choice.bAvailable = false;
				if (Choice.GateHint.IsEmpty())
				{
					Choice.GateHint = FText::FromString(FString::Printf(
						TEXT("(no %s)"), *D.ItemId.ToString().ToUpper()));
				}
			}
		}
		else if (D.Kind == EEclipseStageDirectiveKind::MeterCompareGate)
		{
			const int32 Cur = State->GetMeterValue(D.Stat);
			bool bPass = false;
			const TCHAR* OpSym = TEXT("?");
			switch (D.Op)
			{
			case EEclipseCompareOp::Less:         bPass = Cur <  D.Value; OpSym = TEXT("<");  break;
			case EEclipseCompareOp::LessEqual:    bPass = Cur <= D.Value; OpSym = TEXT("<="); break;
			case EEclipseCompareOp::Equal:        bPass = Cur == D.Value; OpSym = TEXT("==");  break;
			case EEclipseCompareOp::NotEqual:     bPass = Cur != D.Value; OpSym = TEXT("!="); break;
			case EEclipseCompareOp::GreaterEqual: bPass = Cur >= D.Value; OpSym = TEXT(">="); break;
			case EEclipseCompareOp::Greater:      bPass = Cur >  D.Value; OpSym = TEXT(">");  break;
			}
			if (!bPass)
			{
				Choice.bAvailable = false;
				if (Choice.GateHint.IsEmpty())
				{
					Choice.GateHint = FText::FromString(FString::Printf(
						TEXT("(need %s %s %d)"),
						*D.Stat.ToString().ToUpper(), OpSym, D.Value));
				}
			}
		}
		else if (D.Kind == EEclipseStageDirectiveKind::HiddenStatGate)
		{
			// Numeric compare on a hidden social stat (Annoyance). Hidden —
			// when it fails we just close the option, no revealing hint.
			const int32 Cur = State->GetHiddenStatValue(D.Stat);
			bool bPass = false;
			switch (D.Op)
			{
			case EEclipseCompareOp::Less:         bPass = Cur <  D.Value; break;
			case EEclipseCompareOp::LessEqual:    bPass = Cur <= D.Value; break;
			case EEclipseCompareOp::Equal:        bPass = Cur == D.Value; break;
			case EEclipseCompareOp::NotEqual:     bPass = Cur != D.Value; break;
			case EEclipseCompareOp::GreaterEqual: bPass = Cur >= D.Value; break;
			case EEclipseCompareOp::Greater:      bPass = Cur >  D.Value; break;
			}
			if (!bPass)
			{
				// Hidden: no GateHint, and the choice is dropped from the
				// node view entirely (bHiddenGateFailed).
				Choice.bAvailable = false;
				Choice.bHiddenGateFailed = true;
			}
		}
		else if (D.Kind == EEclipseStageDirectiveKind::StatCompareGate)
		{
			// Silent unlock on a visible stat ("ZEN >= 3"): below the bar the
			// option simply doesn't exist — new dialogue appears as the stat
			// grows, with no revealing greyed row or hint.
			const int32 Cur = State->GetStatValue(D.Stat);
			bool bPass = false;
			switch (D.Op)
			{
			case EEclipseCompareOp::Less:         bPass = Cur <  D.Value; break;
			case EEclipseCompareOp::LessEqual:    bPass = Cur <= D.Value; break;
			case EEclipseCompareOp::Equal:        bPass = Cur == D.Value; break;
			case EEclipseCompareOp::NotEqual:     bPass = Cur != D.Value; break;
			case EEclipseCompareOp::GreaterEqual: bPass = Cur >= D.Value; break;
			case EEclipseCompareOp::Greater:      bPass = Cur >  D.Value; break;
			}
			if (!bPass)
			{
				Choice.bAvailable = false;
				Choice.bHiddenGateFailed = true;
			}
		}
		else if (D.Kind == EEclipseStageDirectiveKind::IdentityGate)
		{
			// Name equality on a hidden identity tag (Gender / Race).
			// "GENDER == female" passes only when the player's gender is
			// female; "RACE != brown" closes off when race is brown.
			// Hidden — no revealing GateHint on failure.
			const FName Cur = State->GetIdentityValue(D.Stat);
			const bool bMatch = (Cur == D.ItemId);
			const bool bPass  = (D.Op == EEclipseCompareOp::NotEqual) ? !bMatch : bMatch;
			if (!bPass)
			{
				Choice.bAvailable = false;
				Choice.bHiddenGateFailed = true;
			}
		}
	}
}

FText UEclipseDialogueSubsystem::BuildEffectsLineText(const TArray<FEclipseStageDirective>& Directives)
{
	TArray<FString> Bits;
	for (const FEclipseStageDirective& D : Directives)
	{
		if (D.Kind == EEclipseStageDirectiveKind::StatEffect)
		{
			Bits.Add(FString::Printf(TEXT("%+d %s"), D.Value, *D.Stat.ToString().ToUpper()));
		}
		else if (D.Kind == EEclipseStageDirectiveKind::MeterEffect)
		{
			Bits.Add(FString::Printf(TEXT("%+d %s"), D.Value, *D.Stat.ToString().ToUpper()));
		}
	}
	if (Bits.IsEmpty()) return FText::GetEmpty();
	return FText::FromString(FString::Join(Bits, TEXT("  ·  ")));
}


void UEclipseDialogueSubsystem::DispatchMenuAction(FName ActionName)
{
	UE_LOG(LogEclipse, Log, TEXT("MenuAction: %s"), *ActionName.ToString());

	UEclipseGameStateSubsystem* State = nullptr;
	if (UWorld* W = GetWorld())
		if (UGameInstance* GI = W->GetGameInstance())
			State = GI->GetSubsystem<UEclipseGameStateSubsystem>();
	if (!State) return;

	if (ActionName == TEXT("enterStall"))
	{
		// Player enters the pink stall to meet the Angel. The Angel quest
		// stage flips to "ready" — only "saved" once tabs are given OR the
		// angel battle resolves favourably.
		State->Quest.Stage = TEXT("ready");
		State->NotifyChanged();

		// Trigger the camera/dialogue transition into the Angel encounter.
		EnterStallTransition();
	}
	else if (ActionName == TEXT("giveTabs"))
	{
		// Consume 4 "tabs" items from inventory and mark angel as saved.
		const FName TabId(TEXT("tabs"));
		int32 Removed = 0;
		while (Removed < 4 && State->RemoveItem(TabId)) ++Removed;
		State->Quest.Stage = TEXT("saved");
		State->NotifyChanged();
		UE_LOG(LogEclipse, Log, TEXT("giveTabs: removed %d tab(s)"), Removed);
	}
	else if (ActionName == TEXT("startGame"))
	{
		// Finish intro / current chapter, transition forwards.
		State->OnChapterTransition();   // increments Chapter + clears failed-choice cache

		// Fire a chapter-card overlay through the GameStateSubsystem delegate.
		// EclipseChapterCardWidget listens; ChapterCard fades in 0.6s, holds
		// 2.4s, fades out 0.8s.
		const FString TitleStr = FString::Printf(TEXT("CHAPTER %d"), State->Chapter);
		// First-chapter title — easy to extend with a per-chapter table later.
		FText Title = (State->Chapter == 1)
			? FText::FromString(TEXT("Night Begins"))
			: FText::FromString(FString::Printf(TEXT("Chapter %d"), State->Chapter));
		State->ShowChapterCard(Title);

		// Close any open dialogue so the card has the screen.
		CloseDialogue();

		// Autosave at chapter boundaries — feels like a "checkpoint reached" beat.
		State->SaveCurrent();
	}
	else if (ActionName == TEXT("takeItem"))
	{
		// Item-interaction dialogue's "Take it" choice (see Items.ink) —
		// the close-and-commit equivalent of "startGame" above. Picking up
		// the item is what EclipseInteractSubsystem::TryInteract() used to
		// do directly for every item; now it only happens immediately for
		// items with no DialogueId (see TryInteract).
		if (ActiveItem)
		{
			ActiveItem->Pickup();
		}
		CloseDialogue();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  EnterStallTransition — the AngelSeeker's "open the door" beat.
//
//  When the player picks "Open the stall and enter" with the Hair + Eye in
//  inventory, we:
//   1. Close the AngelSeeker dialogue (panel slides out)
//   2. Tell the AngelSeeker to step aside (clears the doorway)
//
//  The Angel itself is a normal NPC placed in the secret room behind the
//  doorway. The player walks up to it and presses [E] like any other NPC —
//  the Angel dialogue tree (Dlg_Angel) is NOT auto-triggered from here.
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::EnterStallTransition()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Cache the AngelSeeker reference BEFORE CloseDialogue() nulls ActiveNpc,
	// so we can also tell her to step aside out of the doorway.
	AEclipseNpcCharacter* AngelSeeker = ActiveNpc;

	// 1. Close the current (AngelSeeker) dialogue cleanly.
	CloseDialogue();

	// 2. Tell the AngelSeeker to step aside (lerps her out of the doorway).
	if (AngelSeeker && AngelSeeker->bIsAngelSeeker)
	{
		AngelSeeker->StepAside();
	}
}
