// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EclipseDanceStyle.generated.h"

UENUM(BlueprintType)
enum class EEclipseDanceStyle : uint8
{
	Hakken,
	Muzzing,
	Liquid,
	Gloving,
	Tektonik,
	Count UMETA(Hidden)
};

namespace EclipseDance
{
	// Direction bits a style's combo is made of.
	enum EDir : uint8 { Up = 1, Left = 2, Down = 4, Right = 8 };

	struct FStyleInfo
	{
		const TCHAR* Name;
		FColor Color;       // one colour per style so it reads before the name does
		uint8 Keys;         // EDir bits held together
		float WheelDeg;     // 0 = right, 90 = up; singles on the cardinals, combos on the diagonals
		const TCHAR* Combo; // shown on the wheel
	};

	// Adding a style: enum entry above + one row here.
	inline const FStyleInfo& Info(EEclipseDanceStyle S)
	{
		static const FStyleInfo Table[] = {
			{ TEXT("HAKKEN"),   FColor(0xE6, 0x2A, 0x2A), Down,         270.f, TEXT("↓") },     // the feet
			{ TEXT("MUZZING"),  FColor(0xF2, 0xB7, 0x05), Right,          0.f, TEXT("→") },     // right hand
			{ TEXT("LIQUID"),   FColor(0x3F, 0xD0, 0xFF), Left | Right,  45.f, TEXT("←→") }, // both hands
			{ TEXT("GLOVING"),  FColor(0xB3, 0x6B, 0xFF), Left,         180.f, TEXT("←") },     // left hand
			{ TEXT("TEKTONIK"), FColor(0x5C, 0xFF, 0x8D), Up,            90.f, TEXT("↑") },
		};
		static_assert(UE_ARRAY_COUNT(Table) == (int32)EEclipseDanceStyle::Count, "one row per style");
		return Table[FMath::Clamp((int32)S, 0, (int32)EEclipseDanceStyle::Count - 1)];
	}

	inline FText StyleName(EEclipseDanceStyle S) { return FText::FromString(Info(S).Name); }
	inline FLinearColor StyleColor(EEclipseDanceStyle S) { return FLinearColor(Info(S).Color); }
}
