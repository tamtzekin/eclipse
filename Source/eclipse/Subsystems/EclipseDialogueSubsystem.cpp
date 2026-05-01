// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDialogueSubsystem.h"
#include "Eclipse.h"
#include "NPC/EclipseNpcCharacter.h"

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

	// TODO(slice): look up the entry node in UArticyDatabase by GUID and
	// populate CurrentNode (speaker, body, choices).
	CurrentNode = FEclipseDialogueNodeView{};
	CurrentNode.SpeakerName = Npc->NpcName;
	CurrentNode.Body = FText::FromString(TEXT("[PLACEHOLDER] Dialogue runtime not yet wired."));

	OnDialogueOpened.Broadcast(Npc);
	OnNodeChanged.Broadcast(CurrentNode);
	UE_LOG(LogEclipse, Log, TEXT("Opened dialogue '%s' with NPC '%s'"),
		*CurrentDialogueId.ToString(), *Npc->NpcName.ToString());
	return true;
}

bool UEclipseDialogueSubsystem::MakeChoice(int32 ChoiceIndex)
{
	if (!bDialogueOpen) return false;
	if (!CurrentNode.Choices.IsValidIndex(ChoiceIndex)) return false;

	const FEclipseDialogueChoice& Chosen = CurrentNode.Choices[ChoiceIndex];
	UE_LOG(LogEclipse, Log, TEXT("Choice picked: %s"), *Chosen.Text.ToString());

	// TODO(slice): advance Articy node by ChoiceId, repopulate CurrentNode,
	// dispatch menuAction if attached, broadcast OnNodeChanged. If the chosen
	// path leads to a terminal node (no choices, no outgoing), CloseDialogue().
	return true;
}

void UEclipseDialogueSubsystem::CloseDialogue()
{
	if (!bDialogueOpen) return;
	bDialogueOpen = false;
	ActiveNpc = nullptr;
	CurrentDialogueId = NAME_None;
	CurrentNode = FEclipseDialogueNodeView{};
	OnDialogueClosed.Broadcast();
	UE_LOG(LogEclipse, Log, TEXT("Dialogue closed"));
}

void UEclipseDialogueSubsystem::InjectSyntheticDialogues()
{
	// TODO(slice): when the Articy database is loaded, register synthetic node trees
	// for the three IDs that the JS prototype builds at runtime:
	//   0x0100000000000001  Dlg_Intro
	//   0x0100000000000F00  Dlg_AngelSeeker
	//   0x0100000000001000  Dlg_Angel
	// These aren't authored in dialogues.articy.json yet — the JS does it via
	// injectIntroDialogue / injectAngelQuestDialogues. Mirror that here as a
	// fallback until they're authored properly.
	UE_LOG(LogEclipse, Verbose, TEXT("InjectSyntheticDialogues: stub — no Articy DB attached yet"));
}

bool UEclipseDialogueSubsystem::ParseSkillCheck(const FText& ChoiceText, FName& OutStat, int32& OutValue) const
{
	// Match "[WORD:10] ..." | "[RHYTHM:14] ..." | "[SHADOW:10] ..."
	const FString S = ChoiceText.ToString();
	int32 OpenBracket = INDEX_NONE; S.FindChar('[', OpenBracket);
	int32 CloseBracket = INDEX_NONE; S.FindChar(']', CloseBracket);
	if (OpenBracket == INDEX_NONE || CloseBracket == INDEX_NONE || CloseBracket <= OpenBracket + 2) return false;

	const FString Inner = S.Mid(OpenBracket + 1, CloseBracket - OpenBracket - 1);
	int32 Colon = INDEX_NONE; Inner.FindChar(':', Colon);
	if (Colon == INDEX_NONE) return false;

	OutStat = FName(*Inner.Left(Colon).ToLower());
	OutValue = FCString::Atoi(*Inner.Mid(Colon + 1));
	return OutStat == TEXT("word") || OutStat == TEXT("rhythm") || OutStat == TEXT("shadow");
}

void UEclipseDialogueSubsystem::DispatchMenuAction(FName ActionName)
{
	// TODO(slice): wire up handlers for:
	//   enterStall  → progress Angel quest (Stage = "saved" or trigger fight)
	//   giveTabs    → consume 4 tabs from inventory, unlock saved end
	//   startGame   → finishIntro() — fade overlay, show chapter card
	UE_LOG(LogEclipse, Log, TEXT("MenuAction dispatched: %s (stub)"), *ActionName.ToString());
}
