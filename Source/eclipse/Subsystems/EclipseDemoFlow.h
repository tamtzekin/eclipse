// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseDemoFlow.generated.h"

/**
 * Demo-build flow: the two decisions that differ between "I'm iterating in
 * PIE" and "this is the build someone else plays".
 *
 *   1. Does the START screen show?  (PIE: no, build: yes — see UEclipseDemoSettings)
 *   2. Has the demo's END condition tripped, and what happens then?
 *
 * The end sequence is: the Ink quest named by EndTriggerQuest goes active →
 * the screen fades to black → EndingDialogueKnot plays as a normal dialogue
 * over that black → its `-> END` raises the end screen (REPLAY / QUIT).
 *
 * Kept as a GameInstance subsystem so it survives the level change between
 * the start screen and gameplay, and so the poll below has somewhere to live.
 */
UCLASS()
class ECLIPSE_API UEclipseDemoFlow : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** True when the start screen should be presented. False in PIE unless
	 *  UEclipseDemoSettings::bShowStartScreenInPIE is set. */
	UFUNCTION(BlueprintPure, Category = "Eclipse|Demo")
	bool ShouldShowStartScreen() const;

	/** True once the end sequence has begun — stops the poll re-triggering
	 *  and lets other UI (HUD, interact prompts) stand down. */
	UFUNCTION(BlueprintPure, Category = "Eclipse|Demo")
	bool IsEnding() const { return bEnding; }

	/** Begin the end sequence by hand (debug, or an explicit story beat).
	 *  Safe to call twice — the second call is ignored. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Demo")
	void TriggerEnding();

	/** Called by the dialogue widget when the ending knot reaches `-> END`.
	 *  Raises the end screen. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Demo")
	void ShowEndScreen();

	/** REPLAY on the end screen: wipe the run and go back to the start. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Demo")
	void RestartDemo();

private:
	// The Ink SideQuests LIST has no change delegate, so the trigger is
	// polled — same approach and cadence as the HUD's quest checklist.
	// Cheap: one list read, and only while a demo run is actually live.
	bool PollEndCondition(float DeltaSeconds);

	FTSTicker::FDelegateHandle PollHandle;
	bool bEnding = false;
};
