// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDialogueSubsystem.h"
#include "Eclipse.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Player/EclipsePlayerCharacter.h"
#include "EclipseGameStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

// ─────────────────────────────────────────────────────────────────────────────
// Synthetic dialogue node store — lives here until Articy exports are done.
// Structure mirrors Articy's FragmentNode shape: Speaker, Body, Choices[].
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Shared node store, populated once by InjectSyntheticDialogues.
	struct FSyntheticNode
	{
		FName Id;
		FName SpeakerId;    // "NPC" or "PLAYER"
		FString Body;       // NPC speech body
		FString MenuText;   // Player choice label (if SpeakerId=="PLAYER")
		TArray<FName> Outputs;
		FName MenuAction;   // "enterStall" / "giveTabs" / "startGame" / None
		FName SkillCheckStat;   // "aesthetics" / "stimulation" / "rhythm" / "zen" / "psychedelics" / None
		int32 SkillCheckValue = 0;
		// Energy cost if a failed skill check choice is clicked anyway.
		// Designer-tunable per choice; defaults to 5 so unset legacy data
		// still feels mildly punishing without being lethal.
		int32 EnergyDamageOnFail = 5;
	};

	struct FSyntheticDialogue
	{
		FName Id;
		FName FirstNodeId;
	};

	TMap<FName, FSyntheticNode>     GSyntheticNodes;
	TMap<FName, FSyntheticDialogue> GSyntheticDialogues;

	static const FName NPC_SPEAKER(TEXT("NPC"));
	static const FName PLAYER_SPEAKER(TEXT("PLAYER"));

	// ── Helpers ──────────────────────────────────────────────────────────────

	void AddNPC(FName Id, const FString& Body, TArray<FName> Outputs)
	{
		FSyntheticNode n;
		n.Id = Id; n.SpeakerId = NPC_SPEAKER; n.Body = Body; n.Outputs = MoveTemp(Outputs);
		GSyntheticNodes.Add(Id, MoveTemp(n));
	}
	void AddChoice(FName Id, const FString& MenuText, TArray<FName> Outputs,
		FName MenuAction = NAME_None, FName SkillStat = NAME_None, int32 SkillVal = 0)
	{
		FSyntheticNode n;
		n.Id = Id; n.SpeakerId = PLAYER_SPEAKER; n.MenuText = MenuText;
		n.Outputs = MoveTemp(Outputs);
		n.MenuAction = MenuAction; n.SkillCheckStat = SkillStat; n.SkillCheckValue = SkillVal;
		GSyntheticNodes.Add(Id, MoveTemp(n));
	}
	void AddDialogue(FName Id, FName FirstNode)
	{
		GSyntheticDialogues.Add(Id, FSyntheticDialogue{Id, FirstNode});
	}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// InjectSyntheticDialogues — called once at Initialize.
// Mirrors injectAngelQuestDialogues() from index.html ~line 2886.
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::InjectSyntheticDialogues()
{
	if (!GSyntheticNodes.IsEmpty()) return; // already injected

	// ─── ANGEL SEEKER  (0x0100000000000F00) ────────────────────────────────
	// Opening nodes: three depending on quest state (chosen at runtime).
	const FName AS_DLG   (TEXT("0x0100000000000F00"));
	const FName AS_INTRO (TEXT("0x0100000000000F01")); // no items yet
	const FName AS_PROMPT(TEXT("0x0100000000000F02")); // has one item
	const FName AS_READY (TEXT("0x0100000000000F03")); // has both
	const FName AS_CLOSED(TEXT("0x0100000000000F04")); // quest done

	const FName AS_IC1 (TEXT("0x0100000000000F11")); // "What is it?"
	const FName AS_IC2 (TEXT("0x0100000000000F12")); // [Leave]
	const FName AS_IA1 (TEXT("0x0100000000000F13")); // Angel explanation

	const FName AS_PC1 (TEXT("0x0100000000000F21")); // "Not both. What do I need?"
	const FName AS_PA1 (TEXT("0x0100000000000F22")); // "The Hair. The Eye."

	const FName AS_RC1 (TEXT("0x0100000000000F31")); // "Open stall and enter"
	const FName AS_RC2 (TEXT("0x0100000000000F32")); // "Not yet."
	const FName AS_RA1 (TEXT("0x0100000000000F33")); // "Go. Be gentle."

	// Intro branch
	AddNPC(AS_INTRO,
		TEXT("She leans against the stall, ear pressed to the partition. \"Shhh. It's in there. I hear it crying.\""),
		{AS_IC1, AS_IC2});
	AddChoice(AS_IC1, TEXT("What is it?"),  {AS_IA1});
	AddChoice(AS_IC2, TEXT("[Leave]"),       {});
	AddNPC(AS_IA1,
		TEXT("\"An angel. Buried under the club. It needs Angel's Hair — the silver drink upstairs — and its Eye, somewhere on the dance floor. Bring them. I'll let you in.\""),
		{});

	// Partial branch
	AddNPC(AS_PROMPT,
		TEXT("\"You're close. I can feel it. Do you have both?\""),
		{AS_PC1, AS_IC2});
	AddChoice(AS_PC1, TEXT("Not both. What do I still need?"), {AS_PA1});
	AddNPC(AS_PA1,
		TEXT("\"The Hair. The Eye. Both. Without them it won't listen.\""),
		{});

	// Ready branch
	AddNPC(AS_READY,
		TEXT("\"You have them. I smell the silver. I feel the Eye. Go in. Don't lie to it.\""),
		{AS_RC1, AS_RC2});
	AddChoice(AS_RC1, TEXT("[Open the stall and enter]"), {}, TEXT("enterStall"));
	AddChoice(AS_RC2, TEXT("[Not yet]"), {});

	// Post-quest line
	AddNPC(AS_CLOSED, TEXT("\"Thank you. It sleeps now.\""), {});

	// Recognition variants (first meeting while already carrying items)
	const FName AS_RH  (TEXT("0x0100000000000F41")); // has hair only
	const FName AS_RE  (TEXT("0x0100000000000F42")); // has eye only
	const FName AS_RB  (TEXT("0x0100000000000F43")); // has both
	const FName AS_RHC1(TEXT("0x0100000000000F44"));
	const FName AS_REC1(TEXT("0x0100000000000F45"));
	const FName AS_RBC1(TEXT("0x0100000000000F46"));
	const FName AS_RHA1(TEXT("0x0100000000000F47"));
	const FName AS_REA1(TEXT("0x0100000000000F48"));
	const FName AS_RBA1(TEXT("0x0100000000000F49"));
	const FName AS_RBEN(TEXT("0x0100000000000F4A"));

	AddNPC(AS_RH, TEXT("She lifts her head. \"Stop. I can smell the silver on you. The Hair. Who sent you?\""), {AS_RHC1, AS_IC2});
	AddChoice(AS_RHC1, TEXT("I have it."), {AS_RHA1});
	AddNPC(AS_RHA1, TEXT("\"Good. There's an angel buried under the club. It needs that drink — and its Eye. Somewhere out on the dance floor. Bring both. I'll let you in.\""), {});

	AddNPC(AS_RE, TEXT("Her pupils pin. \"The Eye. You walked in carrying the Eye. Do you know what it is?\""), {AS_REC1, AS_IC2});
	AddChoice(AS_REC1, TEXT("I have it."), {AS_REA1});
	AddNPC(AS_REA1, TEXT("\"An angel sleeps under the club. That's its Eye. It also needs its Hair — the silver drink upstairs, in VIP. Bring that too.\""), {});

	AddNPC(AS_RB, TEXT("She rises from the stall, slowly. \"Both. You walked in with both. You don't even know, do you?\""), {AS_RBC1, AS_IC2});
	AddChoice(AS_RBC1, TEXT("I have it."), {AS_RBA1});
	AddNPC(AS_RBA1, TEXT("\"An angel. Buried under the club. It needs the Hair and the Eye. You brought both. Go in. Don't lie to it.\""), {AS_RBEN, AS_RC2});
	AddChoice(AS_RBEN, TEXT("[Open the stall and enter]"), {}, TEXT("enterStall"));

	// Post-intro partial variants
	const FName AS_PH (TEXT("0x0100000000000F51")); // met + has hair
	const FName AS_PE (TEXT("0x0100000000000F52")); // met + has eye
	const FName AS_PHC(TEXT("0x0100000000000F53"));
	const FName AS_PEC(TEXT("0x0100000000000F54"));
	const FName AS_PHA(TEXT("0x0100000000000F55"));
	const FName AS_PEA(TEXT("0x0100000000000F56"));

	AddNPC(AS_PH, TEXT("\"You're close. The silver's on you. The Eye's still missing.\""), {AS_PHC, AS_IC2});
	AddChoice(AS_PHC, TEXT("I have the Hair. Where's the Eye?"), {AS_PHA});
	AddNPC(AS_PHA, TEXT("\"On the dance floor. Cold. Glassy. You'll feel it before you see it.\""), {});

	AddNPC(AS_PE, TEXT("\"You've got the Eye. Now the Hair. The silver drink — upstairs, in VIP.\""), {AS_PEC, AS_IC2});
	AddChoice(AS_PEC, TEXT("I have the Eye. What about the Hair?"), {AS_PEA});
	AddNPC(AS_PEA, TEXT("\"Upstairs. The silver one. They call it Angel's Hair. Buy it, don't drink it.\""), {});

	AddDialogue(AS_DLG, AS_INTRO);

	// ─── ANGEL  (0x0100000000001000) ────────────────────────────────────────
	const FName AN_DLG      (TEXT("0x0100000000001000"));
	const FName AN_INTRO    (TEXT("0x0100000000001001"));
	const FName AN_IC1      (TEXT("0x0100000000001011")); // [WORD:10]
	const FName AN_IC2      (TEXT("0x0100000000001012")); // [SHADOW:10]
	const FName AN_IC3      (TEXT("0x0100000000001013")); // [RHYTHM:10]
	const FName AN_CONV_OK  (TEXT("0x0100000000001014"));
	const FName AN_BATTLE   (TEXT("0x0100000000001015"));
	const FName AN_BC1      (TEXT("0x0100000000001021")); // [WORD:14]
	const FName AN_BC2      (TEXT("0x0100000000001022")); // [SHADOW:14]
	const FName AN_BC3      (TEXT("0x0100000000001023")); // [RHYTHM:14]
	const FName AN_DEFEATED (TEXT("0x0100000000001024"));
	const FName AN_SC1      (TEXT("0x0100000000001031")); // Give 4 tabs
	const FName AN_SC2      (TEXT("0x0100000000001032")); // Walk away
	const FName AN_SAVED    (TEXT("0x0100000000001033"));
	const FName AN_LEFT     (TEXT("0x0100000000001034"));

	// Skill thresholds dropped to baseline so the encounter is always playable
	// with default stats (all five at 1). Tune later as the stat-progression
	// system lands. The [STAT:N] display tag mirrors the SkillCheckStat key.
	// Mapping from the legacy 3-stat labels: Word→Aesthetics,
	// Shadow→Zen, Rhythm→Rhythm. Stimulation / Psychedelics not yet used in
	// authored content — designer can mix them in as new branches land.
	AddNPC(AN_INTRO,
		TEXT("The figure turns without turning. A voice that is many voices: \"YOU. WHO SENT YOU?\""),
		{AN_IC1, AN_IC2, AN_IC3});
	AddChoice(AN_IC1, TEXT("[AESTHETICS:1] I am a messenger of the Mandate."),
		{AN_CONV_OK}, NAME_None, TEXT("aesthetics"), 1);
	AddChoice(AN_IC2, TEXT("[ZEN:1] I bring the Hair and the Eye. A gift."),
		{AN_CONV_OK}, NAME_None, TEXT("zen"),        1);
	AddChoice(AN_IC3, TEXT("[RHYTHM:1] Your song drew me down."),
		{AN_CONV_OK}, NAME_None, TEXT("rhythm"),     1);

	AddNPC(AN_CONV_OK,
		TEXT("\"…THEN SPEAK THE NAME. PROVE IT.\" Its form fragments, reassembles. Ready for battle."),
		{AN_BATTLE});
	AddNPC(AN_BATTLE,
		TEXT("It circles you, glitching through the walls. \"STRIKE, MESSENGER.\""),
		{AN_BC1, AN_BC2, AN_BC3});
	AddChoice(AN_BC1, TEXT("[AESTHETICS:1] Recite the forbidden syllables."),
		{AN_DEFEATED}, NAME_None, TEXT("aesthetics"), 1);
	AddChoice(AN_BC2, TEXT("[ZEN:1] Overwhelm it with your silence."),
		{AN_DEFEATED}, NAME_None, TEXT("zen"),        1);
	AddChoice(AN_BC3, TEXT("[RHYTHM:1] Match its cadence, beat for beat."),
		{AN_DEFEATED}, NAME_None, TEXT("rhythm"),     1);

	AddNPC(AN_DEFEATED,
		TEXT("The angel collapses inward, dimming. It whispers: \"I am… cold. I am dying. Feed me the tabs. Four. Please.\""),
		{AN_SC1, AN_SC2});
	AddChoice(AN_SC1, TEXT("[Give ×4 Tabs]"),     {AN_SAVED}, TEXT("giveTabs"));
	AddChoice(AN_SC2, TEXT("[Leave it to die]"),  {AN_LEFT});

	// AN_SAVED — angel encounter resolved favourably. The player picks a single
	// "Continue" choice that fires startGame → chapter transition + autosave.
	const FName AN_SC_CONTINUE(TEXT("0x0100000000001041"));
	AddNPC(AN_SAVED,
		TEXT("Warmth returns. The angel's wings unfold, pale gold. \"…thank you, messenger.\" It sleeps."),
		{AN_SC_CONTINUE});
	AddChoice(AN_SC_CONTINUE, TEXT("[Step out of the stall]"), {}, TEXT("startGame"));

	AddNPC(AN_LEFT,
		TEXT("You turn your back. Something small and cold winks out behind you."),
		{AN_SC_CONTINUE});

	AddDialogue(AN_DLG, AN_INTRO);

	// ─── STALL VOICE 1  (calm)  0x0100000000000B00 ──────────────────────────
	// Quiet voice from the next stall. A warning, plus optional flavour reply.
	const FName SV1_DLG (TEXT("0x0100000000000B00"));
	const FName SV1_N1  (TEXT("0x0100000000000B01"));
	const FName SV1_C1  (TEXT("0x0100000000000B11"));
	const FName SV1_C2  (TEXT("0x0100000000000B12"));
	const FName SV1_N2  (TEXT("0x0100000000000B13"));

	AddNPC(SV1_N1,
		TEXT("A flat voice from the next stall. \"Don't talk to her. The one with her ear on the door. She isn't listening to anything kind.\""),
		{SV1_C1, SV1_C2});
	AddChoice(SV1_C1, TEXT("Why?"),    {SV1_N2});
	AddChoice(SV1_C2, TEXT("[Leave]"), {});
	AddNPC(SV1_N2,
		TEXT("\"Whatever's in there isn't alone. And it remembers names. Don't give it yours.\""),
		{});
	AddDialogue(SV1_DLG, SV1_N1);

	// ─── STALL VOICE 2  (erratic)  0x0100000000000C00 ───────────────────────
	// Twitchy, fragmented. Two-beat exchange, no useful info but flavour rich.
	const FName SV2_DLG (TEXT("0x0100000000000C00"));
	const FName SV2_N1  (TEXT("0x0100000000000C01"));
	const FName SV2_C1  (TEXT("0x0100000000000C11"));
	const FName SV2_C2  (TEXT("0x0100000000000C12"));
	const FName SV2_N2  (TEXT("0x0100000000000C13"));

	AddNPC(SV2_N1,
		TEXT("A laugh that turns into a cough. \"Hi hi hi. You're new. New ones always taste — sorry. Sorry. I mean SMELL. Smell different.\""),
		{SV2_C1, SV2_C2});
	AddChoice(SV2_C1, TEXT("Are you alright?"), {SV2_N2});
	AddChoice(SV2_C2, TEXT("[Back away]"),       {});
	AddNPC(SV2_N2,
		TEXT("\"Mm. Mm. I'm fine. I'm — fine. Fine fine fine. The mirror in here keeps blinking and I can't tell which side I'm on. But fine.\""),
		{});
	AddDialogue(SV2_DLG, SV2_N1);

	UE_LOG(LogEclipse, Log, TEXT("DialogueSubsystem: %d synthetic nodes, %d dialogues injected."),
		GSyntheticNodes.Num(), GSyntheticDialogues.Num());
}

// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("DialogueSubsystem::Initialize"));
	InjectSyntheticDialogues();
}

void UEclipseDialogueSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("DialogueSubsystem::Deinitialize"));
	Super::Deinitialize();
}

// ─────────────────────────────────────────────────────────────────────────────
// OpenDialogue — picks the correct entry node based on quest state
// (mirrors openDialogue() logic for Angel Seeker in index.html ~line 4161)
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseDialogueSubsystem::OpenDialogue(AEclipseNpcCharacter* Npc)
{
	if (!Npc || Npc->DialogueId == NAME_None)
	{
		UE_LOG(LogEclipse, Warning, TEXT("OpenDialogue: missing NPC or dialogueId"));
		return false;
	}

	ActiveNpc = Npc;
	CurrentDialogueId = Npc->DialogueId;
	bDialogueOpen = true;

	// Pause the chapter clock while dialogue is open — TickChapterClock
	// early-returns on !bClockRunning so no game-time elapses during
	// conversation. CloseDialogue() flips it back on. Each MakeChoice call
	// will still add +1 game-second so reading through a long branch costs
	// time even though the wall clock is frozen.
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->SetClockRunning(false);
		}
	}

	// Determine the correct entry node.
	// For the Angel Seeker we branch on quest state, exactly as in the JS prototype.
	FName EntryNode = NAME_None;

	if (Npc->bIsAngelSeeker)
	{
		UEclipseGameStateSubsystem* State = nullptr;
		if (UWorld* W = GetWorld())
			if (UGameInstance* GI = W->GetGameInstance())
				State = GI->GetSubsystem<UEclipseGameStateSubsystem>();

		const bool bHasHair = State && State->Quest.bHasHair;
		const bool bHasEye  = State && State->Quest.bHasEye;
		const bool bMet     = State && State->HasMetNPC(Npc->NpcName);
		const FName QStage  = State ? State->Quest.Stage : FName(TEXT("intro"));
		const bool bDone    = QStage == TEXT("saved") || QStage == TEXT("done");

		if (bDone)
		{
			EntryNode = FName(TEXT("0x0100000000000F04")); // AS_CLOSED
		}
		else if (bHasHair && bHasEye)
		{
			EntryNode = bMet
				? FName(TEXT("0x0100000000000F03")) // AS_READY
				: FName(TEXT("0x0100000000000F43")); // AS_RECOG_BOTH (first meeting, both items)
		}
		else if (bHasHair)
		{
			EntryNode = bMet
				? FName(TEXT("0x0100000000000F51")) // AS_PROMPT_HAIR (post-intro, has hair)
				: FName(TEXT("0x0100000000000F41")); // AS_RECOG_HAIR (first meeting, has hair)
		}
		else if (bHasEye)
		{
			EntryNode = bMet
				? FName(TEXT("0x0100000000000F52")) // AS_PROMPT_EYE (post-intro, has eye)
				: FName(TEXT("0x0100000000000F42")); // AS_RECOG_EYE (first meeting, has eye)
		}
		else
		{
			EntryNode = bMet
				? FName(TEXT("0x0100000000000F02")) // AS_PROMPT
				: FName(TEXT("0x0100000000000F01")); // AS_INTRO (first time)
		}

		if (State) State->RecordMetNPC(Npc->NpcName, CurrentDialogueId);
	}
	else
	{
		// For non-Angel-Seeker NPCs: find the dialogue root and start from its first fragment.
		if (const FSyntheticDialogue* Dlg = GSyntheticDialogues.Find(CurrentDialogueId))
		{
			EntryNode = Dlg->FirstNodeId;
		}
		else
		{
			// TODO(post-slice): route to UArticyDatabase for Articy-authored dialogues.
			UE_LOG(LogEclipse, Warning, TEXT("OpenDialogue: no synthetic or Articy node for '%s'"),
				*CurrentDialogueId.ToString());
			EntryNode = NAME_None;
		}
	}

	if (EntryNode != NAME_None)
	{
		AdvanceToNode(EntryNode);
	}
	else
	{
		// Fallback placeholder
		CurrentNode = FEclipseDialogueNodeView{};
		CurrentNode.SpeakerName = Npc->NpcName;
		CurrentNode.Body = FText::FromString(TEXT("[Dialogue not yet authored]"));
	}

	// ── OTS rotation: face the NPC (matches JS prototype OTS transition) ──
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			if (AEclipsePlayerCharacter* Player = Cast<AEclipsePlayerCharacter>(PC->GetPawn()))
			{
				Player->StartFaceTarget(Npc->GetActorLocation());
			}
		}
	}

	OnDialogueOpened.Broadcast(Npc);
	// (Don't re-broadcast OnNodeChanged here — AdvanceToNode above already
	// fired it. Re-broadcasting causes the dialogue widget's body-cascade
	// animation to restart mid-flight every time a conversation opens,
	// visible as a brief judder + a doubled "StartBodyAnimation" log pair.
	// Fallback placeholder branch above doesn't call AdvanceToNode, but it
	// doesn't broadcast OnNodeChanged either — the widget's existing default
	// state is fine for the "[Dialogue not yet authored]" case.)
	UE_LOG(LogEclipse, Log, TEXT("Opened dialogue '%s' with NPC '%s' → entry '%s'"),
		*CurrentDialogueId.ToString(), *Npc->NpcName.ToString(), *EntryNode.ToString());
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

	// Skill-check failure tax — if the player clicked a check they didn't
	// have the stat for (bAvailable=false), drain Energy by the per-choice
	// declared amount before we route through. The dialogue widget no
	// longer disables failed-skill buttons; players can attempt risky
	// checks at a cost.
	if (Chosen.bIsSkillCheck && !Chosen.bAvailable && Chosen.EnergyDamageOnFail > 0)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				UE_LOG(LogEclipse, Log, TEXT("Choice: failed skill check '%s' (need %d, got %d) -> -%d energy"),
					*Chosen.SkillCheckStat.ToString(), Chosen.SkillCheckValue,
					GS->GetStatValue(Chosen.SkillCheckStat), Chosen.EnergyDamageOnFail);
				GS->DrainEnergy((float)Chosen.EnergyDamageOnFail);
			}
		}
	}

	// Helper — bump the chapter clock by 20 game-seconds AND drain thirst by
	// a fixed cost. Called only on continuing choices (see below). Exit
	// choices that fall through to CloseDialogue pay neither cost — the
	// live clock just resumes and thirst stays where it was.
	//
	// 20s (not 30) keeps the visible minute readout from ticking cleanly
	// on every 2nd choice — the irregular crossings feel less mechanical.
	// 2.0 thirst/click means ~50 continuing choices empty a full bar, so
	// a few back-to-back conversations push the player toward a drink.
	auto BumpClockForContinue = [this]()
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
			{
				const float Before = GS->ChapterElapsedSeconds;
				GS->ChapterElapsedSeconds += 20.0f;
				GS->DrainThirst(2.0f);
				UE_LOG(LogEclipse, Log, TEXT("Dlg: choice +20s  %.1f -> %.1f  thirst=%.1f"),
					Before, GS->ChapterElapsedSeconds, GS->Thirst);
			}
		}
	};

	// Dispatch menuAction if present
	if (Chosen.ChoiceId != NAME_None)
	{
		if (const FSyntheticNode* Node = GSyntheticNodes.Find(Chosen.ChoiceId))
		{
			if (Node->MenuAction != NAME_None)
			{
				DispatchMenuAction(Node->MenuAction);
			}
			// Advance to the first output of this choice node — counts as
			// a "continuing" click, pay the +30s.
			if (Node->Outputs.Num() > 0)
			{
				BumpClockForContinue();
				AdvanceToNode(Node->Outputs[0]);
				return true;
			}
		}
	}

	// No next node → close (no +30s — the live clock just resumes).
	CloseDialogue();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AdvanceToNode — populate CurrentNode from a synthetic node ID
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::AdvanceToNode(FName NodeId)
{
	const FSyntheticNode* Node = GSyntheticNodes.Find(NodeId);
	if (!Node)
	{
		UE_LOG(LogEclipse, Warning, TEXT("AdvanceToNode: unknown node '%s'"), *NodeId.ToString());
		CloseDialogue();
		return;
	}

	CurrentNodeId = NodeId;

	// Skip player-choice fragments (they become choices in the parent NPC node).
	// Walk forward if this node is an NPC speech.
	if (Node->SpeakerId == NPC_SPEAKER)
	{
		CurrentNode = FEclipseDialogueNodeView{};
		CurrentNode.SpeakerName = ActiveNpc ? ActiveNpc->NpcName : FName(TEXT("NPC"));
		CurrentNode.Body = FText::FromString(Node->Body);

		// Build choices from output nodes that are PLAYER fragments
		UEclipseGameStateSubsystem* State = nullptr;
		if (UWorld* W = GetWorld())
			if (UGameInstance* GI = W->GetGameInstance())
				State = GI->GetSubsystem<UEclipseGameStateSubsystem>();

		for (const FName& OutId : Node->Outputs)
		{
			if (const FSyntheticNode* ChoiceNode = GSyntheticNodes.Find(OutId))
			{
				FEclipseDialogueChoice Choice;
				Choice.ChoiceId = OutId;
				Choice.Text = FText::FromString(ChoiceNode->MenuText);
				Choice.bIsSkillCheck = (ChoiceNode->SkillCheckStat != NAME_None);
				Choice.SkillCheckStat = ChoiceNode->SkillCheckStat;
				Choice.SkillCheckValue = ChoiceNode->SkillCheckValue;
				Choice.EnergyDamageOnFail = ChoiceNode->EnergyDamageOnFail;

				// Evaluate availability of skill-check choices via the
				// 5-stat resolver (returns 0 for unknown keys, which makes
				// the check always fail — fine, that's what we want).
				if (Choice.bIsSkillCheck && State)
				{
					const int32 StatVal = State->GetStatValue(ChoiceNode->SkillCheckStat);
					Choice.bAvailable = (StatVal >= ChoiceNode->SkillCheckValue);
				}
				CurrentNode.Choices.Add(Choice);
			}
		}

		// Always offer at least one choice so the player has a clear way out.
		// Leaf NPC nodes get a synthetic "[Continue]" / "[Goodbye]" option.
		if (CurrentNode.Choices.IsEmpty())
		{
			FEclipseDialogueChoice Continue;
			Continue.ChoiceId      = NAME_None;   // sentinel: MakeChoice will close
			Continue.Text          = FText::FromString(TEXT("[Goodbye]"));
			Continue.bAvailable    = true;
			Continue.bIsSkillCheck = false;
			CurrentNode.Choices.Add(Continue);
		}

		OnNodeChanged.Broadcast(CurrentNode);
	}
	else
	{
		// Player fragment — shouldn't land here directly, but handle gracefully.
		if (Node->Outputs.Num() > 0)
			AdvanceToNode(Node->Outputs[0]);
		else
			CloseDialogue();
	}
}

// ─────────────────────────────────────────────────────────────────────────────

void UEclipseDialogueSubsystem::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	ActiveNpc = nullptr;
	CurrentDialogueId = NAME_None;
	CurrentNode = FEclipseDialogueNodeView{};

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
