// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EclipseChapterDefinition.generated.h"

USTRUCT(BlueprintType)
struct FEclipseChapterRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Index = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Title;       // "CHAPTER I"
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Subtitle;    // "00:00 — 02:00"
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StartMinute = 0;
};
