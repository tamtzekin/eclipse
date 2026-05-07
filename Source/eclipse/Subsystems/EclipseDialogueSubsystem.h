// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseDialogueSubsystem.generated.h"

class AEclipseNpcCharacter;

USTRUCT(BlueprintType)
struct FEclipseDialogueChoice
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName ChoiceId;
	UPROPERTY(BlueprintReadOnly) FText Text;
	UPROPERTY(BlueprintReadOnly) bool bAvailable = true;
	UPROPERTY(BlueprintReadOnly) bool bIsSkillCheck = false;
	UPROPERTY(BlueprintReadOnly) FName SkillCheckStat;   // "word" | "rhythm" | "shadow"
	UPROPERTY(BlueprintReadOnly) int32 SkillCheckValue = 0;
};

USTRUCT(BlueprintType)
struct FEclipseDialogueNodeView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName SpeakerName;
	UPROPERTY(BlueprintReadOnly) FText Body;
	UPROPERTY(BlueprintReadOnly) TArray<FEclipseDialogueChoice> Choices;
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
