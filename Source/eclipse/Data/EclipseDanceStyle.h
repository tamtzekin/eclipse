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
	// Direction bits a style's arrow-key combo is made of.
	enum EDir : uint8 { Up = 1, Left = 2, Down = 4, Right = 8 };

	struct FStyleInfo
	{
		const TCHAR* Name;
		FColor Color;       // one colour per style so it reads before the name does
		FColor Deep;        // the dark end of its gradient: quiet parts of the wave, shadowed UI
		uint8 Keys;         // EDir bits held together on the arrows / D-pad
		float WheelDeg;     // 0 = right, 90 = up; singles on the cardinals, combos on the diagonals; also the right-stick angle
		const TCHAR* Combo; // arrow glyphs shown on the wheel
		const TCHAR* KeyHint; // spelled out for the tutorial
	};

	// Adding a style: enum entry above + one row here.
	inline const FStyleInfo& Info(EEclipseDanceStyle S)
	{
		static const FStyleInfo Table[] = {
			{ TEXT("HAKKEN"),   FColor(0xFF, 0x1A, 0x1A), FColor(0x5C, 0x00, 0x08), Down,         270.f, TEXT("\u2193"),        TEXT("DOWN") },          // the feet
			{ TEXT("MUZZING"),  FColor(0x04, 0xA1, 0xFE), FColor(0x00, 0x20, 0xBC), Right,          0.f, TEXT("\u2192"),        TEXT("RIGHT") },
			{ TEXT("LIQUID"),   FColor(0x00, 0xA8, 0xFF), FColor(0x00, 0x3A, 0x70), Left | Right,  45.f, TEXT("\u2190\u2192"), TEXT("LEFT + RIGHT") },  // both hands
			{ TEXT("GLOVING"),  FColor(0xB0, 0x26, 0xFF), FColor(0x3A, 0x00, 0x70), Left,         180.f, TEXT("\u2190"),        TEXT("LEFT") },
			{ TEXT("TEKTONIK"), FColor(0xF2, 0xB8, 0x5C), FColor(0x6A, 0x3A, 0x0A), Up,            90.f, TEXT("\u2191"),        TEXT("UP") },
		};
		static_assert(UE_ARRAY_COUNT(Table) == (int32)EEclipseDanceStyle::Count, "one row per style");
		return Table[FMath::Clamp((int32)S, 0, (int32)EEclipseDanceStyle::Count - 1)];
	}

	inline FText StyleName(EEclipseDanceStyle S) { return FText::FromString(Info(S).Name); }
	inline FLinearColor StyleColor(EEclipseDanceStyle S) { return FLinearColor(Info(S).Color); }
	inline FLinearColor StyleDeep(EEclipseDanceStyle S) { return FLinearColor(Info(S).Deep); }

	// Switch judging, For Honor style: the window opens a bar early and PERFECT is right on the switch. Song seconds/beats.
	// Players land a little late (reaction + audio delay), so the ideal sits just after the downbeat.
	constexpr float JudgeOffset = 0.1f;
	constexpr float PerfectHalf = 0.15f;       // PERFECT within this of the ideal
	constexpr float SwitchWindowBeats = 4.f;   // GOOD from this many beats before the switch: the wave's colour blend spans exactly this
	constexpr float GoodLate = 0.3f;           // and up to this long after the ideal
}
