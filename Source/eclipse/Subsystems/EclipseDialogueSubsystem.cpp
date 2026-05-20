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

// ── Articy runtime — generic interfaces only ──
// We deliberately avoid #including any project-specific generated header
// (UEclipse_9000Dialogue etc.) so this TU compiles whether or not the
// ArticyXImporter has been re-run. Property access goes through the
// IArticy* interfaces shared by all generated classes.
#include "ArticyDatabase.h"
#include "ArticyObject.h"
#include "ArticyPins.h"
#include "ArticyBuiltinTypes.h"   // UArticyOutgoingConnection::GetTarget
#include "Interfaces/ArticyObjectWithText.h"
#include "Interfaces/ArticyObjectWithMenuText.h"
#include "Interfaces/ArticyObjectWithStageDirections.h"
#include "Interfaces/ArticyObjectWithSpeaker.h"
#include "Interfaces/ArticyOutputPinsProvider.h"

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
		// Raw Articy "Stage directions" string — parsed at OpenDialogue /
		// AdvanceToNode time. Comma-separated tokens, see the doc on
		// EEclipseStageDirectiveKind for the grammar.
		FString StageDirections;
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

	void AddNPC(FName Id, const FString& Body, TArray<FName> Outputs,
		const FString& StageDirections = TEXT(""))
	{
		FSyntheticNode n;
		n.Id = Id; n.SpeakerId = NPC_SPEAKER; n.Body = Body; n.Outputs = MoveTemp(Outputs);
		n.StageDirections = StageDirections;
		GSyntheticNodes.Add(Id, MoveTemp(n));
	}
	void AddChoice(FName Id, const FString& MenuText, TArray<FName> Outputs,
		FName MenuAction = NAME_None, FName SkillStat = NAME_None, int32 SkillVal = 0,
		const FString& StageDirections = TEXT(""))
	{
		FSyntheticNode n;
		n.Id = Id; n.SpeakerId = PLAYER_SPEAKER; n.MenuText = MenuText;
		n.Outputs = MoveTemp(Outputs);
		n.MenuAction = MenuAction; n.SkillCheckStat = SkillStat; n.SkillCheckValue = SkillVal;
		n.StageDirections = StageDirections;
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

	// ─── ENLIGHTENED RAVER  (Articy Dlg_0E4D4F6A) ──────────────────────────
	// Lightweight synthetic stub seeded from the body text in
	// Content/Dialogues/ECLIPSE 9000.articyue. Keys use the Articy
	// TechnicalName so when the ArticyXImporter generator bug is fixed
	// (its "Feature"-suffix duplication) and the runtime resolution
	// switches to UArticyDatabase, the same DialogueId on the NPC actor
	// will start routing to the full Articy-authored flow without scene edits.
	const FName ER_DLG  (TEXT("Dlg_0E4D4F6A"));
	const FName ER_INTRO(TEXT("Dlg_0E4D4F6A.Intro"));
	const FName ER_C1   (TEXT("Dlg_0E4D4F6A.C1"));   // "Hey, are you in line?"
	const FName ER_C2   (TEXT("Dlg_0E4D4F6A.C2"));   // "[Get down on her level]"
	const FName ER_N2   (TEXT("Dlg_0E4D4F6A.N2"));   // Raver looks up
	const FName ER_C3   (TEXT("Dlg_0E4D4F6A.C3"));   // "I'll help you find it"
	const FName ER_N3   (TEXT("Dlg_0E4D4F6A.N3"));   // "I need the angel's hair, the angel's eye"

	AddNPC(ER_INTRO,
		TEXT("Her face is pressed to the bathroom floor. All the water and piss nearly touching her cheek — it makes you retch."),
		{ER_C1, ER_C2});
	AddChoice(ER_C1, TEXT("Hey, are you in line?"),       {ER_N2});
	AddChoice(ER_C2, TEXT("[Get down on her level]"),     {ER_N2});
	AddNPC(ER_N2,
		TEXT("She stands. Her face is so… bright? Something is up — the way she stares into you, like she sees the universe in your eyes. \"I'm looking for the angel. There's an angel, under this club.\""),
		{ER_C3});
	AddChoice(ER_C3, TEXT("I'll help you find it."), {ER_N3});
	AddNPC(ER_N3,
		TEXT("\"I need the angel's hair — the parts of itself it sheds when witnessed by the mass. And its eye. Bring me both.\""),
		{});
	AddDialogue(ER_DLG, ER_INTRO);

	// ─── DAESUNG (the rich guy is passed out)  (Articy Dlg_97F8ED64) ───────
	// Mirrors the four top-level player choices the user authored in
	// ECLIPSE 9000.articyue under "Flow/The rich guy is passed out". IDs use
	// the actual Articy TechnicalNames (DFr_*) so future swap-in to a live
	// UArticyDatabase lookup keeps the same identifiers.
	const FName DS_DLG     (TEXT("Dlg_97F8ED64"));
	const FName DS_INTRO   (TEXT("Dlg_97F8ED64.Intro"));   // Daesung body (from DFr_4D2AE56D)

	const FName DS_KICK    (TEXT("DFr_317564BC"));   // "Kick him."
	const FName DS_HELLO   (TEXT("DFr_3858D8DC"));   // "Hello...?"   [ZEN: 1]
	const FName DS_KNOCK   (TEXT("DFr_A46B0787"));   // "Knock him with the bottle."  [EMPTY_BOTTLE]
	const FName DS_POUR    (TEXT("DFr_8C3C7336"));   // "Pour water over his head."   [BOTTLE_OF_WATER]

	// Response fragments — Tomas narration shown after the player clicks.
	// Suffix ".R" so the response sits under the same Articy id namespace.
	const FName DS_KICK_R  (TEXT("DFr_317564BC.R"));
	const FName DS_HELLO_R (TEXT("DFr_3858D8DC.R"));
	const FName DS_KNOCK_R (TEXT("DFr_A46B0787.R"));
	const FName DS_POUR_R  (TEXT("DFr_8C3C7336.R"));

	// Body text lifted verbatim from Articy DFr_4D2AE56D.
	AddNPC(DS_INTRO,
		TEXT("Nothing. Completely lifeless, this guy, lying on the floor in his own piss, and other people's piss. A total embarrassment.\n\nYou wonder if he's down there for a reason."),
		{DS_KICK, DS_HELLO, DS_KNOCK, DS_POUR});

	// Four player choices. Stage directions on three of them — Hello requires
	// ZEN, Knock requires an empty bottle, Pour requires a bottle of water.
	// All four taken verbatim from the Articy MenuText + StageDirections fields.
	AddChoice(DS_KICK,  TEXT("Kick him."),                       {DS_KICK_R});
	AddChoice(DS_HELLO, TEXT("Hello...?"),                       {DS_HELLO_R},
		NAME_None, NAME_None, 0, TEXT("[ZEN: 1]"));
	AddChoice(DS_KNOCK, TEXT("Knock him with the bottle."),      {DS_KNOCK_R},
		NAME_None, NAME_None, 0, TEXT("[EMPTY_BOTTLE]"));
	AddChoice(DS_POUR,  TEXT("Pour water over his head."),       {DS_POUR_R},
		NAME_None, NAME_None, 0, TEXT("[BOTTLE_OF_WATER]"));

	// Response narrations. Two have Articy Description text; the other two
	// are blank in the .articyue today — we ship a "(…)" placeholder so the
	// flow doesn't dead-end visually. Replace with the real Description the
	// moment the author fills it in.
	AddNPC(DS_KICK_R,  TEXT("With a bit of force, your foot lands in the spot between his ribs."), {});
	AddNPC(DS_HELLO_R, TEXT("Hey, are you alright?"),                                              {});
	AddNPC(DS_KNOCK_R, TEXT("(…)"),                                                                {});
	AddNPC(DS_POUR_R,  TEXT("(…)"),                                                                {});

	AddDialogue(DS_DLG, DS_INTRO);

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
		// Prefer the Articy database when it's been imported — that's the
		// authored source of truth. Falls through to the synthetic store
		// when the db isn't loaded yet or doesn't have the id.
		const FName ArticyEntry = ResolveArticyDialogueEntry(CurrentDialogueId);
		if (ArticyEntry != NAME_None)
		{
			UE_LOG(LogEclipse, Log, TEXT("OpenDialogue: '%s' resolved via Articy db → entry '%s'"),
				*CurrentDialogueId.ToString(), *ArticyEntry.ToString());
			EntryNode = ArticyEntry;
		}
		else if (const FSyntheticDialogue* Dlg = GSyntheticDialogues.Find(CurrentDialogueId))
		{
			EntryNode = Dlg->FirstNodeId;
		}
		else
		{
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

	// Apply stage-directive effects from this choice. Effects fire on click
	// for every choice (available OR gated — gated choices still consume
	// their cost so designers can author "you tried and failed" branches
	// that pay a stat tax). Gates that disabled the choice in the widget
	// mean the button is greyed out and click never reaches here anyway.
	for (const FEclipseStageDirective& D : Chosen.StageDirectives)
	{
		const bool bIsEffect =
			D.Kind == EEclipseStageDirectiveKind::StatEffect ||
			D.Kind == EEclipseStageDirectiveKind::EnergyEffect;
		if (bIsEffect)
		{
			ApplyStageEffect(D);
		}
	}

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
	CurrentNodeId = NodeId;

	// ── Articy-first resolution ──
	// When the Articy db is loaded and has this NodeId, use the authored
	// content (Description + StageDirections + OutputPins → choice list).
	// Each choice fragment's MenuText is then read on the same db pass.
	// Falls through to synthetic stubs only if the db doesn't know the id.
	{
		FEclipseDialogueNodeView ArticyView;
		TArray<FName> ArticyChoiceIds;
		if (ResolveArticyNode(NodeId, ArticyView, ArticyChoiceIds))
		{
			CurrentNode = ArticyView;

			UEclipseGameStateSubsystem* State = nullptr;
			if (UWorld* W = GetWorld())
				if (UGameInstance* GI = W->GetGameInstance())
					State = GI->GetSubsystem<UEclipseGameStateSubsystem>();

			for (const FName& ChoiceNodeId : ArticyChoiceIds)
			{
				FEclipseDialogueChoice Choice;
				Choice.ChoiceId = ChoiceNodeId;

				// Pull MenuText + StageDirections from the Articy choice
				// fragment via the same interface scaffolding ResolveArticyNode
				// uses. Done inline here (rather than another helper) because
				// we only need two properties per child.
				if (UArticyDatabase* DB = UArticyDatabase::Get(this))
				{
					if (UArticyObject* ChoiceObj = DB->GetObjectByName(ChoiceNodeId))
					{
						// These two interface getters are plain BlueprintCallable
						// virtuals (not BlueprintNativeEvent), so calling via
						// the interface pointer dispatches correctly — UHT
						// doesn't generate a usable Execute_* helper for them.
						if (IArticyObjectWithMenuText* WithMenu = Cast<IArticyObjectWithMenuText>(ChoiceObj))
						{
							Choice.Text = WithMenu->GetMenuText();
						}
						if (IArticyObjectWithStageDirections* WithSD = Cast<IArticyObjectWithStageDirections>(ChoiceObj))
						{
							const FString SDStr = WithSD->GetStageDirections().ToString();
							Choice.StageDirectives = ParseStageDirections(SDStr);
						}
					}
				}
				if (Choice.Text.IsEmpty())
				{
					// Empty MenuText means this output is a "CONTINUE" path
					// (NPC response, not a player choice). Show it as such.
					Choice.Text = FText::FromString(TEXT("→  CONTINUE"));
				}

				EvaluateChoiceGates(Choice);
				CurrentNode.Choices.Add(Choice);
			}

			// Same goodbye fallback as the synthetic path so the player
			// always has a way to exit a dead-end fragment.
			if (CurrentNode.Choices.IsEmpty())
			{
				FEclipseDialogueChoice Goodbye;
				Goodbye.ChoiceId = NAME_None;
				Goodbye.Text     = FText::FromString(TEXT("[Goodbye]"));
				Goodbye.bAvailable = true;
				CurrentNode.Choices.Add(Goodbye);
			}

			OnNodeChanged.Broadcast(CurrentNode);
			return;
		}
	}

	// ── Synthetic fallback ──
	const FSyntheticNode* Node = GSyntheticNodes.Find(NodeId);
	if (!Node)
	{
		UE_LOG(LogEclipse, Warning, TEXT("AdvanceToNode: unknown node '%s'"), *NodeId.ToString());
		CloseDialogue();
		return;
	}

	// Skip player-choice fragments (they become choices in the parent NPC node).
	// Walk forward if this node is an NPC speech.
	if (Node->SpeakerId == NPC_SPEAKER)
	{
		CurrentNode = FEclipseDialogueNodeView{};
		CurrentNode.SpeakerName = ActiveNpc ? ActiveNpc->NpcName : FName(TEXT("NPC"));
		CurrentNode.Body = FText::FromString(Node->Body);

		// Parse the NPC fragment's own stage directions. Effect directives
		// roll into the EffectsLine the widget renders in orange below the
		// body. Gate directives on an NPC fragment are unusual (gates are
		// normally on player choices) but we still attach them so authors
		// who put them there see them applied.
		{
			TArray<FEclipseStageDirective> BodyDirectives = ParseStageDirections(Node->StageDirections);
			CurrentNode.EffectsLine = BuildEffectsLineText(BodyDirectives);
		}

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

				// Stage directives: parse + attach + evaluate gates.
				// EvaluateChoiceGates only flips bAvailable to FALSE; it
				// never re-enables a choice that the skill-check above
				// already disqualified. GateHint is populated with the
				// first failing gate's "(need …)" message.
				Choice.StageDirectives = ParseStageDirections(ChoiceNode->StageDirections);
				EvaluateChoiceGates(Choice);

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

// ─────────────────────────────────────────────────────────────────────────────
//  Stage-directions plumbing (parser + eval + apply + display-string builder)
//
//  Grammar (comma-separated tokens, whitespace flexible):
//    [STAT_NAME: N]   StatGate     — gates availability
//    [ITEM_NAME]      ItemGate     — gates availability
//    +N STAT_NAME     StatEffect   — applied on click
//    -N STAT_NAME     StatEffect   — applied on click
//    +N ENERGY        EnergyEffect — applied on click
//    -N ENERGY        EnergyEffect — applied on click
//
//  STAT_NAME ∈ { AESTHETICS, STIMULATION, RHYTHM, ZEN, PSYCHEDELICS }
//  ITEM_NAME = DT_Items row id in ALL CAPS (matched case-insensitively against
//  the lowercased inventory ids).
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Recognise the five gameplay stats. ENERGY is a separate effect kind, not
	// part of GetStatValue's set.
	bool IsKnownStatKey(FName Lower)
	{
		return Lower == TEXT("aesthetics")
			|| Lower == TEXT("stimulation")
			|| Lower == TEXT("rhythm")
			|| Lower == TEXT("zen")
			|| Lower == TEXT("psychedelics");
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

		// Effect forms start with '+' or '-' (signed integer, then a STAT name
		// or ENERGY). We tolerate any amount of internal whitespace.
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
			if (Target == TEXT("energy"))
			{
				D.Kind = EEclipseStageDirectiveKind::EnergyEffect;
				D.Stat = Target;
			}
			else if (IsKnownStatKey(Target))
			{
				D.Kind = EEclipseStageDirectiveKind::StatEffect;
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
	case EEclipseStageDirectiveKind::EnergyEffect:
		// DrainEnergy(Amount) drains by Amount. A "+N ENERGY" effect = restore
		// N, so we drain -N (negative amount → addition). DrainEnergy clamps.
		State->DrainEnergy(static_cast<float>(-Eff.Value));
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
		else if (D.Kind == EEclipseStageDirectiveKind::EnergyEffect)
		{
			Bits.Add(FString::Printf(TEXT("%+d ENERGY"), D.Value));
		}
	}
	if (Bits.IsEmpty()) return FText::GetEmpty();
	return FText::FromString(FString::Join(Bits, TEXT("  ·  ")));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Articy runtime lookup — generic interface-driven, no project-types
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Resolve a Pin → its target node (other side of the connection). Returns
	// null if the pin has no connection or the target hasn't been loaded.
	UArticyObject* ResolvePinTarget(UArticyOutputPin* Pin)
	{
		if (!Pin) return nullptr;
		for (UArticyOutgoingConnection* Conn : Pin->Connections)
		{
			if (!Conn) continue;
			if (UArticyObject* Target = Cast<UArticyObject>(Conn->GetTarget()))
			{
				return Target;
			}
		}
		return nullptr;
	}
}

FName UEclipseDialogueSubsystem::ResolveArticyDialogueEntry(FName DialogueId) const
{
	UArticyDatabase* DB = UArticyDatabase::Get(this);
	if (!DB) return NAME_None;

	UArticyObject* DlgObj = DB->GetObjectByName(DialogueId);
	if (!DlgObj) return NAME_None;

	// A Dialogue node has an OutputPinsProvider; first connection → entry frag.
	// GetOutputPins is a BlueprintNativeEvent — invoke via the Execute_* thunk,
	// not the interface pointer (the latter asserts inside ArticyRuntime).
	if (DlgObj->GetClass()->ImplementsInterface(UArticyOutputPinsProvider::StaticClass()))
	{
		for (UArticyOutputPin* Pin : IArticyOutputPinsProvider::Execute_GetOutputPins(DlgObj))
		{
			if (UArticyObject* Entry = ResolvePinTarget(Pin))
			{
				return Entry->GetTechnicalName();
			}
		}
	}
	return NAME_None;
}

bool UEclipseDialogueSubsystem::ResolveArticyNode(FName NodeId,
	FEclipseDialogueNodeView& OutNode,
	TArray<FName>& OutNextChoiceIds) const
{
	UArticyDatabase* DB = UArticyDatabase::Get(this);
	if (!DB) return false;

	UArticyObject* Node = DB->GetObjectByName(NodeId);
	if (!Node) return false;

	// IArticyObjectWith{Text,MenuText,StageDirections,Speaker} expose plain
	// BlueprintCallable virtual getters with default implementations — they
	// can be called directly through the interface pointer (UHT doesn't
	// generate a usable Execute_* helper for non-NativeEvent UFUNCTIONs).
	// IArticyOutputPinsProvider::GetOutputPins is the one exception below.

	// Body text (Description) — IArticyObjectWithText is on every fragment.
	FText Body;
	if (IArticyObjectWithText* WithText = Cast<IArticyObjectWithText>(Node))
	{
		Body = WithText->GetText();
	}
	OutNode.Body = Body;

	// Stage directions — Articy stores as FText; we run the same parser the
	// synthetic path uses so the rest of the pipeline (gate eval, effect
	// apply, orange line) is unchanged.
	if (IArticyObjectWithStageDirections* WithSD = Cast<IArticyObjectWithStageDirections>(Node))
	{
		const FString SDStr = WithSD->GetStageDirections().ToString();
		const TArray<FEclipseStageDirective> BodyDirectives = ParseStageDirections(SDStr);
		OutNode.EffectsLine = BuildEffectsLineText(BodyDirectives);
	}

	// Speaker name — show the speaker's TechnicalName upper-cased to match
	// the existing in-game speaker style (e.g. "DAESUNG"). Articy speakers
	// are Entity objects with their own TechnicalName/DisplayName; falling
	// back to the active NPC's name when missing.
	if (IArticyObjectWithSpeaker* WithSpeaker = Cast<IArticyObjectWithSpeaker>(Node))
	{
		if (UArticyObject* Speaker = WithSpeaker->GetSpeaker())
		{
			OutNode.SpeakerName = Speaker->GetTechnicalName();
		}
	}
	if (OutNode.SpeakerName == NAME_None)
	{
		OutNode.SpeakerName = ActiveNpc ? ActiveNpc->NpcName : FName(TEXT("NPC"));
	}

	// Walk output pins for next-step targets — these become the choices the
	// subsystem will eventually render. Caller continues with its existing
	// choice-build loop (which expects FName ids it can later resolve again).
	OutNextChoiceIds.Reset();
	if (Node->GetClass()->ImplementsInterface(UArticyOutputPinsProvider::StaticClass()))
	{
		for (UArticyOutputPin* Pin : IArticyOutputPinsProvider::Execute_GetOutputPins(Node))
		{
			if (UArticyObject* Target = ResolvePinTarget(Pin))
			{
				OutNextChoiceIds.Add(Target->GetTechnicalName());
			}
		}
	}

	UE_LOG(LogEclipse, Verbose, TEXT("Articy resolve '%s' → body=%d chars, %d outputs"),
		*NodeId.ToString(), Body.ToString().Len(), OutNextChoiceIds.Num());
	return true;
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
