// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EclipseUiBuilder.generated.h"

/**
 * Editor-only utility for populating empty Widget Blueprints with the layouts
 * defined in our C++ widget classes. Run once after creating a fresh WBP, then
 * the WBP becomes editable in the designer with all the named children that
 * BindWidgetOptional expects.
 *
 * Usage from Python:
 *   import unreal
 *   unreal.EclipseUiBuilder.populate_dialogue_wbp("/Game/Justin/UI/WBP_Dialogue")
 *   unreal.EclipseUiBuilder.populate_hud_wbp     ("/Game/Justin/UI/WBP_HUD")
 *   unreal.EclipseUiBuilder.populate_interact_wbp("/Game/Justin/UI/WBP_InteractPrompt")
 */
UCLASS()
class ECLIPSE_API UEclipseUiBuilder : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateDialogueWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateHUDWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateInteractWBP(const FString& WBPAssetPath);

	/**
	 * Builds a UFont composite that wraps a UFontFace so Slate text widgets
	 * (which expect UFont, not raw UFontFace) can resolve glyphs.
	 *
	 * UE 5.6's AssetImportTask only creates the UFontFace .uasset when
	 * importing .ttf/.otf — the wrapping UFont needs to be constructed
	 * manually. Run once per face:
	 *
	 *   import unreal
	 *   face = unreal.load_asset("/Game/Justin/UI/Fonts/BMSPA")  # UFontFace
	 *   unreal.EclipseUiBuilder.build_font_composite(face,
	 *       "/Game/Justin/UI/Fonts", "BMSPA_Font")
	 */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static class UFont* BuildFontComposite(class UFontFace* FontFace,
		const FString& PackagePath, const FString& AssetName);
};
