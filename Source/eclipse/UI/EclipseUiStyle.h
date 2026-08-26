// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/Font.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateColor.h"
#include "Math/Color.h"
#include "Materials/MaterialInterface.h"

/**
 * Centralised UI tokens — colors and fonts pulled directly from the HTML
 * prototype's CSS. Keep this file as the single source of truth so both
 * widgets and any future styling stays in sync.
 *
 * Source: /Users/j/code/dungeon-prototype/index.html
 */
namespace EclipseUI
{
	// ── Colors (CSS values in comments) ──────────────────────────────────────
	inline const FLinearColor Cyan        = FLinearColor(0.318f, 0.933f, 0.988f, 1.f); // #51eefc
	inline const FLinearColor CyanDim     = FLinearColor(0.318f, 0.933f, 0.988f, 0.5f);
	inline const FLinearColor Cream       = FLinearColor(0.945f, 0.929f, 0.851f, 1.f); // #f1ecd9 chalk
	inline const FLinearColor CreamDim    = FLinearColor(0.945f, 0.929f, 0.851f, 0.85f);

	// ── Inventory palette: hyperlink blue on white ─────────────────────
	// Authored from sRGB hex via FLinearColor(FColor) — writing these as
	// raw floats would gamma-lift them into pastels on screen.
	// LinkBlue on PaperWhite is ~9.7:1, so body text at small sizes holds up.
	inline const FLinearColor LinkBlue    = FLinearColor(FColor(0x0B, 0x3F, 0xC4)); // #0b3fc4
	inline const FLinearColor LinkBlueDim = FLinearColor(FColor(0x0B, 0x3F, 0xC4)).CopyWithNewOpacity(0.55f);
	inline const FLinearColor PaperWhite  = FLinearColor(FColor(0xF7, 0xF7, 0xFA)); // panel ground
	inline const FLinearColor InkWhite    = FLinearColor::White;                    // text ON blue
	inline const FLinearColor Slate       = FLinearColor(0.055f, 0.059f, 0.071f, 1.f); // #0e0f12
	inline const FLinearColor SlateDeep   = FLinearColor(0.031f, 0.035f, 0.047f, 1.f); // #08090c
	inline const FLinearColor PanelBg     = FLinearColor(0.024f, 0.055f, 0.141f, 0.6f); // rgba(6,14,36,0.6)
	inline const FLinearColor PanelBorder = FLinearColor(0.102f, 0.227f, 0.361f, 1.f); // #1a3a5c
	inline const FLinearColor BarTrack    = FLinearColor(0.051f, 0.106f, 0.180f, 1.f); // #0d1b2e
	inline const FLinearColor HeatLow     = FLinearColor(0.078f, 0.235f, 0.549f, 1.f); // #143c8c
	inline const FLinearColor HeatHigh    = Cyan;
	inline const FLinearColor LabelHeat   = FLinearColor(0.427f, 0.604f, 0.780f, 1.f); // #6d9ac7
	inline const FLinearColor LabelThirst = Cyan;
	// Dialogue accent — the game's stall-door red (#e62a2a, materials sheet).
	// Replaced the cyan accents on the dialogue UI (speaker captions,
	// portrait outline) per design direction.
	inline const FLinearColor DialogueRed = FLinearColor(0.902f, 0.165f, 0.165f, 1.f); // #e62a2a

	// Per-stat hue — shared by the dialogue widget's skill-check choice tints
	// and its stat-altering body-text callouts (e.g. "Aesthetics Damaged: ...")
	// so both use the exact same palette. Unset when Stat isn't one of the
	// four known stats.
	inline TOptional<FLinearColor> StatHue(FName Stat)
	{
		if (Stat == TEXT("aesthetics"))   return FLinearColor(1.00f, 0.42f, 0.72f);   // pink
		if (Stat == TEXT("rhythm"))       return FLinearColor(1.00f, 0.80f, 0.30f);   // gold
		if (Stat == TEXT("zen"))          return FLinearColor(0.45f, 0.75f, 1.00f);   // sky blue
		if (Stat == TEXT("psychedelics")) return FLinearColor(0.72f, 0.45f, 1.00f);   // violet
		return {};
	}

	// Same four stats, deep enough to read as body text on a WHITE ground —
	// the dialogue choice cards. The StatHue pastels above are tuned for
	// dark panels and wash out completely there. Written as sRGB hex and
	// converted, because FLinearColor literals are LINEAR: a value that
	// looks dark as a float gamma-lifts to a pastel on screen. Each of
	// these clears WCAG AA (6.2:1 or better) against white.
	inline TOptional<FLinearColor> StatHueDeep(FName Stat)
	{
		if (Stat == TEXT("aesthetics"))   return FLinearColor(FColor(0xB0, 0x15, 0x5F));  // deep rose
		if (Stat == TEXT("rhythm"))       return FLinearColor(FColor(0x8A, 0x55, 0x00));  // bronze
		if (Stat == TEXT("zen"))          return FLinearColor(FColor(0x0F, 0x5C, 0x99));  // deep blue
		if (Stat == TEXT("psychedelics")) return FLinearColor(FColor(0x6A, 0x2B, 0xB5));  // violet
		return {};
	}

	// ── Fonts ────────────────────────────────────────────────────────────────
	// Loaded from /Game/Justin/UI/Fonts. UE 5.6's AssetImportTask creates the
	// UFontFace .uasset, but Slate text widgets want a UFont composite that
	// wraps the face. UEclipseUiBuilder::BuildFontComposite generates the
	// composite once; we then load that composite from these paths.
	inline UFont* GetBMSPA()
	{
		static TWeakObjectPtr<UFont> Cache;
		if (!Cache.IsValid())
			Cache = LoadObject<UFont>(nullptr, TEXT("/Game/Justin/UI/Fonts/BMSPA_Font.BMSPA_Font"));
		return Cache.Get();
	}
	inline UFont* GetRodinPro()
	{
		static TWeakObjectPtr<UFont> Cache;
		if (!Cache.IsValid())
			Cache = LoadObject<UFont>(nullptr, TEXT("/Game/Justin/UI/Fonts/RodinPro_Font.RodinPro_Font"));
		return Cache.Get();
	}
	inline UFont* GetBerenjena()
	{
		static TWeakObjectPtr<UFont> Cache;
		if (!Cache.IsValid())
			Cache = LoadObject<UFont>(nullptr,
				TEXT("/Game/Justin/UI/Fonts/BerenjenaTRIAL-Medium_Font.BerenjenaTRIAL-Medium_Font"));
		return Cache.Get();
	}

	inline FSlateFontInfo MakeBMSPA(int32 Size, float LetterSpacingPx = 0.f)
	{
		FSlateFontInfo Info;
		if (UFont* F = GetBMSPA())
		{
			Info = FSlateFontInfo(F, Size, FName("Default"));
		}
		else
		{
			Info.Size = Size;
		}
		// Slate's LetterSpacing is in 1/1000th em units; CSS px ~= 62.5 * px.
		Info.LetterSpacing = (int32)(LetterSpacingPx * 62.5f);
		return Info;
	}
	inline FSlateFontInfo MakeRodin(int32 Size, float LetterSpacingPx = 0.f)
	{
		FSlateFontInfo Info;
		if (UFont* F = GetRodinPro())
		{
			Info = FSlateFontInfo(F, Size, FName("Default"));
		}
		else
		{
			Info.Size = Size;
		}
		Info.LetterSpacing = (int32)(LetterSpacingPx * 62.5f);
		return Info;
	}
	inline FSlateFontInfo MakeBerenjena(int32 Size, float LetterSpacingPx = 0.f)
	{
		FSlateFontInfo Info;
		if (UFont* F = GetBerenjena())
		{
			Info = FSlateFontInfo(F, Size, FName("Default"));
		}
		else
		{
			Info.Size = Size;
		}
		Info.LetterSpacing = (int32)(LetterSpacingPx * 62.5f);
		return Info;
	}

	// ── Brush helpers ────────────────────────────────────────────────────────
	// Use RoundedBox even for "solid" fills — Box mode requires a texture and
	// renders as nothing if no resource is assigned. RoundedBox is procedural,
	// so we get a flat coloured rectangle with zero corner radius.
	inline FSlateBrush SolidBrush(const FLinearColor& Color)
	{
		FSlateBrush B;
		B.DrawAs    = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Color);
		B.OutlineSettings.Color        = FSlateColor(FLinearColor::Transparent);
		B.OutlineSettings.Width        = 0.f;
		B.OutlineSettings.CornerRadii  = FVector4(0,0,0,0);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return B;
	}
	// Shared TAB-hold rim-glow material — applied via SetOverlayMaterial on
	// every UMeshComponent of NPCs / Items. Cached on first call.
	inline UMaterialInterface* GetHighlightOverlay()
	{
		static TWeakObjectPtr<UMaterialInterface> Cache;
		if (!Cache.IsValid())
		{
			Cache = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/Justin/Materials/M_HighlightOverlay.M_HighlightOverlay"));
		}
		return Cache.Get();
	}

	inline FSlateBrush RoundedBrush(const FLinearColor& Fill, const FLinearColor& Outline,
		float OutlineWidth = 1.f, float CornerRadius = 4.f)
	{
		FSlateBrush B;
		B.DrawAs    = ESlateBrushDrawType::RoundedBox;
		B.TintColor = FSlateColor(Fill);
		B.OutlineSettings.Color        = FSlateColor(Outline);
		B.OutlineSettings.Width        = OutlineWidth;
		B.OutlineSettings.CornerRadii  = FVector4(CornerRadius, CornerRadius, CornerRadius, CornerRadius);
		B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		return B;
	}
}
