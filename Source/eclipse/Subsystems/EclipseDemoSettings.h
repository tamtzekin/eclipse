// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EclipseDemoSettings.generated.h"

/**
 * Demo-build flow control — Project Settings → Game → Eclipse Demo.
 *
 * The problem this solves: the packaged demo needs to open on a START screen
 * and finish on an END screen, but sitting through both on every PIE run
 * while iterating is miserable. Rather than commenting things in and out
 * before a build, the framing is *always compiled in* and gated here, with
 * PIE opting out by default.
 *
 * A UDeveloperSettings (not a subsystem) because these are authored config
 * values, not runtime state: they show up in Project Settings, save to
 * DefaultGame.ini, and are readable from anywhere without a world. The
 * runtime *decisions* live in UEclipseDemoFlow (a GameInstance subsystem)
 * which reads this.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Eclipse Demo"))
class ECLIPSE_API UEclipseDemoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return FName("Game"); }

	// Master switch for the demo framing (start screen + end screen). Off
	// means the game runs as a plain sandbox with neither.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow")
	bool bDemoFlowEnabled = true;

	// The start screen shows in packaged builds. In PIE it's skipped, so a
	// Play press drops you straight into whatever level you're editing —
	// which is the whole point. Flip this on when you specifically want to
	// test the menu without cooking a build.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow",
		meta = (EditCondition = "bDemoFlowEnabled"))
	bool bShowStartScreenInPIE = false;

	// Ink knot played on the black screen once the end condition trips.
	// Its `-> END` is what raises the end screen.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow",
		meta = (EditCondition = "bDemoFlowEnabled"))
	FName EndingDialogueKnot = TEXT("get_into_club");

	// When this Ink SideQuests entry becomes active, the demo is over.
	// Named rather than hard-coded so the ending can be moved to a different
	// beat without a rebuild.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow",
		meta = (EditCondition = "bDemoFlowEnabled"))
	FName EndTriggerQuest = TEXT("get_into_club");

	// Level the START screen lives in — used by the end screen's REPLAY.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow")
	FName StartLevel = TEXT("L_MainMenu");

	// Level a new demo run begins in.
	UPROPERTY(config, EditAnywhere, Category = "Demo Flow")
	FName FirstPlayableLevel = TEXT("L_Club");

	static const UEclipseDemoSettings& Get()
	{
		return *GetDefault<UEclipseDemoSettings>();
	}
};
