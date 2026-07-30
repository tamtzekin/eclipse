// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseCharacterDefinition.generated.h"

/**
 * Canonical lowercase identity tags. Gender / Race on a character row are
 * open FName fields (any value works), but content authors, the Select
 * Screen dropdowns, and dialogue IdentityGate comparisons should all draw
 * from these exact spellings so a "RACE == brown" gate actually lines up
 * with the character's tag. Extend the arrays here as the cast grows —
 * adding a value is data, not a schema change.
 */
namespace EclipseIdentity
{
	inline const TArray<FName>& Races()
	{
		static const TArray<FName> R = {
			TEXT("white"),
			TEXT("brown"),
			TEXT("black"),
			TEXT("asian"),
			TEXT("latino"),
			TEXT("ambiguous"),
		};
		return R;
	}

	inline const TArray<FName>& Genders()
	{
		static const TArray<FName> G = {
			TEXT("male"),
			TEXT("female"),
			TEXT("feminine"),
			TEXT("masculine"),
		};
		return G;
	}

	inline bool IsKnownRace(FName Tag)   { return Races().Contains(Tag); }
	inline bool IsKnownGender(FName Tag) { return Genders().Contains(Tag); }
}

/**
 * A playable character the player picks from the (not-yet-built) Select
 * Screen. Carries the hidden social identity that shapes how NPCs talk to
 * the player and which dialogue branches open:
 *
 *   Gender / Race      — identity tags (lowercase FName, open-ended:
 *                        "female"/"male"/"nonbinary", "brown"/"white"/…).
 *                        Read by IdentityGate Scene-Direction gates.
 *   StartingAnnoyance  — baseline temperament, 0 (bored) .. 10 (annoyed).
 *                        The runtime Annoyance stat is initialised from
 *                        this on selection, then moves during play via
 *                        "+N ANNOYANCE" stage effects.
 *
 * Authored in a DataTable (DT_Characters) keyed by row id. The Select
 * Screen will list these rows and call
 * UEclipseGameStateSubsystem::SelectCharacter(rowId) on pick — that's the
 * framework hook the UI plugs into later.
 */
USTRUCT(BlueprintType)
struct FEclipseCharacterRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText DisplayName;

	// Hidden identity tags consumed by dialogue IdentityGate directives.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Gender = TEXT("unset");
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Race   = TEXT("unset");

	// Baseline 0 (bored) .. 10 (annoyed). Seeds the runtime Annoyance stat.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "10"))
	int32 StartingAnnoyance = 0;

	// Optional flavour for the Select Screen — not required by the gate
	// system; here so the roster is self-describing when the UI lands.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Bio;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> Portrait;
};
