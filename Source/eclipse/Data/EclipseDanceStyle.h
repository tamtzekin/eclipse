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
	Tektonik,
	Jumpstyle,
	Shuffle,
	Count UMETA(Hidden)
};

namespace EclipseDance
{
	struct FStyleInfo
	{
		const TCHAR* Name;
		FColor Color;       // one colour per style so it reads before the name does
		FColor Deep;        // the dark end of its gradient: quiet parts of the wave, shadowed UI
		float WheelDeg;     // 0 = right, 90 = up: the right-stick angle that picks it
		bool bHeavy;        // heavy styles are legs, light styles are hands: which attacks they favour
	};

	// Adding a style: enum entry above + one row here.
	inline const FStyleInfo& Info(EEclipseDanceStyle S)
	{
		static const FStyleInfo Table[] = {
			{ TEXT("HAKKEN"),    FColor(0xFF, 0x1A, 0x1A), FColor(0x5C, 0x00, 0x08), 270.f, true },   // stamping feet
			{ TEXT("MUZZING"),   FColor(0x04, 0xA1, 0xFE), FColor(0x00, 0x20, 0xBC),            45.f, false },
			{ TEXT("LIQUID"),    FColor(0x00, 0xA8, 0xFF), FColor(0x00, 0x3A, 0x70),           0.f, false },   // both hands
			{ TEXT("TEKTONIK"),  FColor(0xF2, 0xB8, 0x5C), FColor(0x6A, 0x3A, 0x0A),             90.f, false },
			{ TEXT("JUMPSTYLE"), FColor(0xFF, 0x6A, 0x1A), FColor(0x5A, 0x1E, 0x00),          135.f, true },
			{ TEXT("SHUFFLE"),   FColor(0x4E, 0xD1, 0x4E), FColor(0x0A, 0x3A, 0x0A),         225.f, true },   // fast steps
		};
		static_assert(UE_ARRAY_COUNT(Table) == (int32)EEclipseDanceStyle::Count, "one row per style");
		return Table[FMath::Clamp((int32)S, 0, (int32)EEclipseDanceStyle::Count - 1)];
	}

	inline FText StyleName(EEclipseDanceStyle S) { return FText::FromString(Info(S).Name); }
	inline FLinearColor StyleColor(EEclipseDanceStyle S) { return FLinearColor(Info(S).Color); }
	inline FLinearColor StyleDeep(EEclipseDanceStyle S) { return FLinearColor(Info(S).Deep); }
	inline bool IsHeavy(EEclipseDanceStyle S) { return Info(S).bHeavy; }

	// One attack in a moveset: when it lands, in beats from the pattern's start, and whether it's legs (heavy) or hands (light).
	struct FNote
	{
		float Beat = 0.f;
		bool bHeavy = false;
	};

	// Moveset notation: "L L H" is crotchets, "L-L" quavers, "L=L" semiquavers, "L*L" the 1st and 3rd of a triplet.
	// The separator sets the gap to the NEXT note, so "L L L-L" is three on the beat then one an eighth later.
	inline TArray<FNote> ParseMoveset(const FString& Pattern)
	{
		TArray<FNote> Notes;
		float Beat = 0.f;
		for (int32 i = 0; i < Pattern.Len(); ++i)
		{
			const TCHAR C = Pattern[i];
			if (C != TEXT('L') && C != TEXT('l') && C != TEXT('H') && C != TEXT('h')) continue;
			Notes.Add({ Beat, C == TEXT('H') || C == TEXT('h') });
			// The character right after the note sets the gap to the next one.
			const TCHAR Next = i + 1 < Pattern.Len() ? Pattern[i + 1] : TEXT(' ');
			Beat += Next == TEXT('-') ? 0.5f : Next == TEXT('=') ? 0.25f : Next == TEXT('*') ? 2.f / 3.f : 1.f;
		}
		return Notes;
	}

	// How the pattern reads above his head.
	inline FString MovesetText(const TArray<FNote>& Notes)
	{
		FString Out;
		for (const FNote& N : Notes) Out += N.bHeavy ? TEXT("DOWN ") : TEXT("UP ");   // the key you answer with
		return Out.TrimEnd();
	}

	// Switch judging, For Honor style: the window opens a bar early and PERFECT is right on the switch. Song seconds/beats.
	// Players land a little late (reaction + audio delay), so the ideal sits just after the downbeat.
	constexpr float JudgeOffset = 0.1f;
	constexpr float PerfectHalf = 0.15f;       // PERFECT within this of the ideal
	constexpr float SwitchWindowBeats = 4.f;   // GOOD from this many beats before the switch: the wave's colour blend spans exactly this
	constexpr float GoodLate = 0.3f;           // and up to this long after the ideal
}
