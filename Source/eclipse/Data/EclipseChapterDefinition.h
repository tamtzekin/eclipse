// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "EclipseChapterDefinition.generated.h"

/**
 * DataTable row form (legacy/CSV-driven). Kept for tools that already
 * import chapter rows via DataTable. New work prefers the UDataAsset
 * below — one .uasset per chapter in /Game/Justin/Data/Chapters/.
 */
USTRUCT(BlueprintType)
struct FEclipseChapterRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Index = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;       // "CHAPTER I"
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Subtitle;    // "00:00 — 02:00"
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartMinute = 0;
};

/**
 * Per-chapter authoring data. Designers create one UDataAsset per chapter
 * in /Game/Justin/Data/Chapters/, fill in title + duration, then drop the
 * lot into UEclipseGameStateSubsystem::ChapterTable (index 0 = Chapter 0).
 *
 * Optional: if no asset is provided for a given chapter index, the clock
 * falls back to DefaultChapterDurationSeconds and a "Chapter N" title.
 *
 * Future fields (deferred): per-chapter music, lighting preset name,
 * weather rolls, NPC shuffle seed, postprocess overrides, dialogue gates.
 */
UCLASS(BlueprintType)
class ECLIPSE_API UEclipseChapterDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// Display title shown on the chapter-card overlay (e.g. "Night Begins",
	// "Last Call"). Falls back to "Chapter N" if empty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Chapter")
	FText DisplayName;

	// Real-seconds the chapter lasts before auto-advancing. <=0 means use
	// the subsystem's DefaultChapterDurationSeconds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Chapter",
		meta = (ClampMin = "0.0"))
	float DurationSeconds = 0.f;

	// Small subtitle label rendered above the title on the chapter card
	// (e.g. "CHAPTER 1", "00:00 — 02:00").
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Chapter")
	FText SubtitleLabel;
};
