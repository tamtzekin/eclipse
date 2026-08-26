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

	/** Variant of PopulateDialogueWBP that bakes a "speech bubble" look:
	 *  outer panel transparent (no PanelFade layers), body text wrapped in
	 *  a black-cloud BodyBubble border, choice buttons styled as bubbles.
	 *  Designer-iterable starting point — runs into the same WBP names so
	 *  C++ BindWidgetOptional bindings still resolve. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateDialogueBubblesWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateHUDWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateInteractWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulatePauseMenuWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateMainMenuWBP(const FString& WBPAssetPath);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateInventoryWBP(const FString& WBPAssetPath);

	/** Swap prompt — the modal that opens when you reach for a pickup with
	 *  nothing free to put it in. Frame only: title, the incoming item's
	 *  name, an empty CandidateRow, and LEAVE IT. The side-by-side
	 *  "SWAP X FOR Y" boxes are built at runtime into CandidateRow, because
	 *  how many there are depends on what the player is carrying — see
	 *  UEclipseSwapPromptWidget::BuildCandidateBoxes. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateSwapPromptWBP(const FString& WBPAssetPath);

	/** Stats panel (C key) — modal centred overlay built in the HUD's visual
	 *  language: navy PanelBg / cream-border RoundedBrush, BMSPA caps, cyan
	 *  highlights. Layout:
	 *    Title "STATS"
	 *    Portrait (HUD-style 90×112 frame with "YOU") | 5-stat column
	 *    HEAT + THIRST vertical bars (HUD-style)
	 *    Currency row + CLOSE button.
	 *  Widget names match the C++ widget's BindWidgetOptional UPROPERTYs.    */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulateStatsMenuWBP(const FString& WBPAssetPath);

	/** Phone overlay (P key) — left-edge 280×460 panel mirroring the
	 *  prototype's `#phone-ui`. Layout: header strip ("PHONE" + ×),
	 *  status row (clock + chapter label left, wallet right), tab row
	 *  (CONTACTS / NOTES), scrolling content placeholder, footer
	 *  action row (CALL / TEXT, disabled stubs). Widget names match
	 *  UEclipsePhoneWidget's BindWidgetOptional UPROPERTYs. */
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UiBuilder",
		meta = (DevelopmentOnly))
	static bool PopulatePhoneWBP(const FString& WBPAssetPath);

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
