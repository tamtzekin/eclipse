// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseUiBuilder.h"
#include "Eclipse.h"

#if WITH_EDITOR
#include "EclipseUiStyle.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "UObject/Package.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "UI/EclipseInventoryWidget.h"   // UEclipseClothingSlotWidget
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"

namespace
{
	// Helper: load a UWidgetBlueprint asset, run a build callback that operates
	// on its WidgetTree, then mark dirty + recompile + save.
	template<typename TBuildFn>
	bool DoBuild(const FString& AssetPath, TBuildFn Build)
	{
		UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(StaticLoadObject(
			UWidgetBlueprint::StaticClass(), nullptr, *AssetPath));
		if (!WBP)
		{
			UE_LOG(LogEclipse, Error, TEXT("UiBuilder: failed to load WBP '%s'"), *AssetPath);
			return false;
		}
		if (!WBP->WidgetTree)
		{
			UE_LOG(LogEclipse, Error, TEXT("UiBuilder: WBP '%s' has no WidgetTree"), *AssetPath);
			return false;
		}

		// Wipe whatever's already there so successive runs are idempotent.
		// Rename ALL existing widget subobjects out to the transient package so
		// their names are freed up. (Just resetting RootWidget = nullptr leaves
		// the named subobjects in place; if we then create a new widget of a
		// different class at the same name, UE asserts:
		//   "Cannot replace existing object of a different class.")
		{
			TArray<UWidget*> ToEvict;
			WBP->WidgetTree->GetAllWidgets(ToEvict);
			for (UWidget* W : ToEvict)
			{
				if (W && IsValid(W))
				{
					W->Rename(nullptr, GetTransientPackage(),
						REN_DontCreateRedirectors | REN_DoNotDirty | REN_NonTransactional);
				}
			}
			WBP->WidgetTree->RootWidget = nullptr;
		}

		Build(WBP, WBP->WidgetTree);

		WBP->MarkPackageDirty();
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
		FKismetEditorUtilities::CompileBlueprint(WBP);

		// Persist to disk so an editor restart doesn't revert to the stale
		// on-disk version. Without this the populator's changes only live
		// in memory until the user manually saves the asset.
		if (UPackage* Pkg = WBP->GetOutermost())
		{
			TArray<UPackage*> ToSave;
			ToSave.Add(Pkg);
			const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty=*/ false);
			UE_LOG(LogEclipse, Log, TEXT("UiBuilder: save '%s' -> %s"),
				*AssetPath, bSaved ? TEXT("ok") : TEXT("FAILED"));
		}

		UE_LOG(LogEclipse, Log, TEXT("UiBuilder: populated '%s'"), *AssetPath);
		return true;
	}

	// Convenience: construct a widget as a child of the WBP's WidgetTree
	template<typename T>
	T* New(UWidgetTree* Tree, FName Name)
	{
		return Tree->ConstructWidget<T>(T::StaticClass(), Name);
	}
}
#endif // WITH_EDITOR

// ─────────────────────────────────────────────────────────────────────────────
//  Dialogue WBP
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateDialogueWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// ── Dialogue panel — right third of the viewport ──
		// To fake a radial fade-from-edges (Slate has no native gradient brush)
		// we stack multiple inset rounded-black layers in a UOverlay. Each
		// successive layer is inset further and uses a higher alpha, so the
		// visible edge is faint and the centre is a near-solid black panel
		// with rounded corners.
		UOverlay* Panel = New<UOverlay>(Tree, TEXT("DialoguePanel"));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			// Anchor-stretched to the right third so the panel tracks
			// viewport width instead of a fixed pixel size.
			S->SetAnchors(FAnchors(2.f / 3.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		}

		// Layer fade definition — { padding-from-edge, alpha, corner-radius }
		struct FLayer { float Pad; float Alpha; float Radius; FName Name; };
		const FLayer Layers[] = {
			{  0.f, 0.10f, 28.f, TEXT("PanelFade_0") },
			{  6.f, 0.25f, 24.f, TEXT("PanelFade_1") },
			{ 14.f, 0.45f, 20.f, TEXT("PanelFade_2") },
			{ 22.f, 0.65f, 16.f, TEXT("PanelFade_3") },
			{ 30.f, 0.80f, 12.f, TEXT("PanelFade_4") },
		};
		for (const FLayer& L : Layers)
		{
			UBorder* B = New<UBorder>(Tree, L.Name);
			B->SetBrush(RoundedBrush(
				FLinearColor(0.f, 0.f, 0.f, L.Alpha),     // black with progressive alpha
				FLinearColor::Transparent,                  // no outline
				0.f, L.Radius));
			B->SetPadding(FMargin(0.f));
			if (UOverlaySlot* OS = Panel->AddChildToOverlay(B))
			{
				OS->SetPadding(FMargin(L.Pad));
				OS->SetHorizontalAlignment(HAlign_Fill);
				OS->SetVerticalAlignment(VAlign_Fill);
			}
		}

		// Vertical column inside panel — sits on top of the fade stack
		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("DialogueColumn"));
		if (UOverlaySlot* OS = Panel->AddChildToOverlay(Column))
		{
			OS->SetPadding(FMargin(38.f, 32.f));   // content padding clear of the fade ring
			OS->SetHorizontalAlignment(HAlign_Fill);
			OS->SetVerticalAlignment(VAlign_Fill);
		}

		// ── Speaker portrait — flush against the panel's LEFT edge. ──
		// ── Texture is set per-conversation by the C++ handler reading ──
		// ── the active NPC's PortraitTexture. ──
		// Portrait photo aspect (~5:7 → 220×308). Right edge of the portrait
		// sits exactly on the 2/3-viewport anchor line (= panel left edge),
		// vertically centred.
		UImage* SpeakerPortrait = New<UImage>(Tree, TEXT("SpeakerPortrait"));
		FSlateBrush PortraitBrush;
		PortraitBrush.DrawAs    = ESlateBrushDrawType::RoundedBox;
		PortraitBrush.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
		PortraitBrush.OutlineSettings.CornerRadii  = FVector4(6, 6, 6, 6);
		PortraitBrush.OutlineSettings.Color        = FSlateColor(DialogueRed);
		PortraitBrush.OutlineSettings.Width        = 2.f;
		PortraitBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		PortraitBrush.ImageSize = FVector2D(220.f, 308.f);
		SpeakerPortrait->SetBrush(PortraitBrush);

		USizeBox* PortraitSize = New<USizeBox>(Tree, TEXT("SpeakerPortraitSize"));
		PortraitSize->SetWidthOverride(220.f);
		PortraitSize->SetHeightOverride(308.f);
		PortraitSize->AddChild(SpeakerPortrait);

		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(PortraitSize))
		{
			S->SetAnchors(FAnchors(2.f / 3.f, 0.5f, 2.f / 3.f, 0.5f)); // panel left edge, vertical center
			S->SetAlignment(FVector2D(1.f, 0.5f));            // right-middle of portrait at anchor
			S->SetPosition(FVector2D(0.f, 0.f));
			S->SetSize(FVector2D(220.f, 308.f));
			S->SetZOrder(3);                                  // above the panel fade stack
		}

		// Speaker name (BMSPA, red accent)
		UTextBlock* SpeakerNameText = New<UTextBlock>(Tree, TEXT("SpeakerNameText"));
		SpeakerNameText->SetFont(MakeBMSPA(18, 3.f));
		SpeakerNameText->SetColorAndOpacity(FSlateColor(DialogueRed));
		SpeakerNameText->SetText(FText::FromString(TEXT("SPEAKER")));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(SpeakerNameText))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f));

		// Body text — primary path is BodyWords (a UWrapBox of per-word
		// UTextBlocks that fade in cascade-style at runtime; see
		// EclipseDialogueWidget::StartBodyAnimation). BodyText is left in the
		// tree as a hidden legacy fallback so designers can still pull a single
		// text block from the WBP if they need to disable the animation.
		UWrapBox* BodyWords = New<UWrapBox>(Tree, TEXT("BodyWords"));
		BodyWords->SetInnerSlotPadding(FVector2D(0.f, 0.f));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyWords))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 14.f));

		// Hidden fallback — never rendered when BodyWords is bound.
		UTextBlock* BodyText = New<UTextBlock>(Tree, TEXT("BodyText"));
		BodyText->SetFont(MakeRodin(14));
		BodyText->SetColorAndOpacity(FSlateColor(Cream));
		BodyText->SetAutoWrapText(true);
		BodyText->SetVisibility(ESlateVisibility::Collapsed);
		BodyText->SetText(FText::GetEmpty());
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyText))
			S->SetPadding(FMargin(0.f));

		// Divider line
		UBorder* Divider = New<UBorder>(Tree, TEXT("ChoicesDivider"));
		Divider->SetBrush(SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.3f)));
		Divider->SetPadding(FMargin(0.f));
		USizeBox* DivSize = New<USizeBox>(Tree, TEXT("ChoicesDividerSize"));
		DivSize->SetHeightOverride(1.f);
		DivSize->AddChild(Divider);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(DivSize))
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 8.f));

		// Choices container — VBox with 3 pre-styled rows. C++ binds and toggles
		// visibility per row at runtime; designer can re-skin each row freely.
		// (Slots 4 & 5 still exist as ChoiceBtn_3 / ChoiceBtn_4 properties on the
		// C++ class for nodes that need more than 3, but only 3 are rendered by
		// default in the designer template.)
		UVerticalBox* ChoicesBox = New<UVerticalBox>(Tree, TEXT("ChoicesBox"));
		Column->AddChildToVerticalBox(ChoicesBox);

		for (int32 i = 0; i < 3; ++i)
		{
			const FString IdxStr = FString::FromInt(i);

			UButton* Btn = New<UButton>(Tree, FName(*FString::Printf(TEXT("ChoiceBtn_%d"), i)));
			FButtonStyle BtnStyle;
			BtnStyle.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.04f));
			BtnStyle.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.10f));
			BtnStyle.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.16f));
			BtnStyle.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.02f));
			Btn->SetStyle(BtnStyle);

			UHorizontalBox* Row = New<UHorizontalBox>(Tree,
				FName(*FString::Printf(TEXT("ChoiceRow_%d"), i)));

			// Circle number node (24x24)
			UBorder* CircleBg = New<UBorder>(Tree,
				FName(*FString::Printf(TEXT("ChoiceCircle_%d"), i)));
			CircleBg->SetBrush(RoundedBrush(
				FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),
				FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),
				1.f, 12.f));
			CircleBg->SetPadding(FMargin(0.f));
			CircleBg->SetHorizontalAlignment(HAlign_Center);
			CircleBg->SetVerticalAlignment(VAlign_Center);

			UTextBlock* CircleNum = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("ChoiceNum_%d"), i)));
			CircleNum->SetText(FText::AsNumber(i + 1));
			CircleNum->SetFont(MakeBMSPA(11));
			CircleNum->SetColorAndOpacity(FSlateColor(Cream));
			CircleNum->SetJustification(ETextJustify::Center);
			CircleBg->SetContent(CircleNum);

			USizeBox* CircleSize = New<USizeBox>(Tree,
				FName(*FString::Printf(TEXT("ChoiceCircleSize_%d"), i)));
			CircleSize->SetWidthOverride(24.f);
			CircleSize->SetHeightOverride(24.f);
			CircleSize->AddChild(CircleBg);

			if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(CircleSize))
			{
				HS->SetPadding(FMargin(0.f, 1.f, 10.f, 0.f));
				HS->SetVerticalAlignment(VAlign_Top);
			}

			// Choice text label (designer-styleable)
			UTextBlock* ChoiceLabel = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("ChoiceText_%d"), i)));
			ChoiceLabel->SetText(FText::FromString(FString::Printf(
				TEXT("Choice %d (placeholder — runtime sets actual text)"), i + 1)));
			ChoiceLabel->SetFont(MakeRodin(14));
			ChoiceLabel->SetColorAndOpacity(FSlateColor(Cream));
			ChoiceLabel->SetAutoWrapText(true);
			if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(ChoiceLabel))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			Btn->SetContent(Row);
			if (UVerticalBoxSlot* VS = ChoicesBox->AddChildToVerticalBox(Btn))
			{
				VS->SetPadding(FMargin(0.f, 4.f));
			}
		}

		// (Close button retired — dialogue exits only through dialogue
		// options like [Leave] / [Goodbye]. The C++ widget also collapses
		// any CloseButton still present in an older bake.)
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Dialogue WBP — speech-bubble variant
//  Same widget-name skeleton as PopulateDialogueWBP (so the C++ widget's
//  BindWidgetOptional bindings still resolve), but:
//    • DialoguePanel UOverlay has no PanelFade_N backdrop layers — fully
//      transparent. The bubbles below are the only things that paint pixels.
//    • BodyWords is wrapped in a "BodyBubble" UBorder — black @ 60% alpha,
//      faint chalk outline, generous padding.
//    • Each ChoiceBtn_N uses a bubble brush (rounded black with progressive
//      alpha for normal/hover/pressed) instead of cream-tint flats.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateDialogueBubblesWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// ── Outer panel — transparent layout guide. No PanelFade backdrop.
		UOverlay* Panel = New<UOverlay>(Tree, TEXT("DialoguePanel"));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			// Anchor-stretched to the right third so the panel tracks
			// viewport width instead of a fixed pixel size.
			S->SetAnchors(FAnchors(2.f / 3.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("DialogueColumn"));
		if (UOverlaySlot* OS = Panel->AddChildToOverlay(Column))
		{
			OS->SetPadding(FMargin(38.f, 32.f));
			OS->SetHorizontalAlignment(HAlign_Fill);
			OS->SetVerticalAlignment(VAlign_Fill);
		}

		// ── Speaker portrait — same as solid version. Anchored to right edge,
		//    overlapping the panel's left side.
		UImage* SpeakerPortrait = New<UImage>(Tree, TEXT("SpeakerPortrait"));
		FSlateBrush PortraitBrush;
		PortraitBrush.DrawAs    = ESlateBrushDrawType::RoundedBox;
		PortraitBrush.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
		PortraitBrush.OutlineSettings.CornerRadii  = FVector4(6, 6, 6, 6);
		PortraitBrush.OutlineSettings.Color        = FSlateColor(DialogueRed);
		PortraitBrush.OutlineSettings.Width        = 2.f;
		PortraitBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		PortraitBrush.ImageSize = FVector2D(220.f, 308.f);
		SpeakerPortrait->SetBrush(PortraitBrush);

		USizeBox* PortraitSize = New<USizeBox>(Tree, TEXT("SpeakerPortraitSize"));
		PortraitSize->SetWidthOverride(220.f);
		PortraitSize->SetHeightOverride(308.f);
		PortraitSize->AddChild(SpeakerPortrait);

		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(PortraitSize))
		{
			// Flush against the panel's left edge (2/3-viewport anchor line).
			S->SetAnchors(FAnchors(2.f / 3.f, 0.5f, 2.f / 3.f, 0.5f));
			S->SetAlignment(FVector2D(1.f, 0.5f));
			S->SetPosition(FVector2D(0.f, 0.f));
			S->SetSize(FVector2D(220.f, 308.f));
			S->SetZOrder(3);
		}

		// ── Speaker name (BMSPA, red accent) — plain text on transparent background.
		UTextBlock* SpeakerNameText = New<UTextBlock>(Tree, TEXT("SpeakerNameText"));
		SpeakerNameText->SetFont(MakeBMSPA(18, 3.f));
		SpeakerNameText->SetColorAndOpacity(FSlateColor(DialogueRed));
		SpeakerNameText->SetText(FText::FromString(TEXT("SPEAKER")));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(SpeakerNameText))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f));

		// ── BodyBubble: black-cloud border wrapping BodyWords.
		UBorder* BodyBubble = New<UBorder>(Tree, TEXT("BodyBubble"));
		BodyBubble->SetBrush(RoundedBrush(
			FLinearColor(0.f, 0.f, 0.f, 0.60f),
			FLinearColor(0.945f, 0.929f, 0.851f, 0.18f),
			1.f, 12.f));
		BodyBubble->SetPadding(FMargin(14.f, 10.f));

		UWrapBox* BodyWords = New<UWrapBox>(Tree, TEXT("BodyWords"));
		BodyWords->SetInnerSlotPadding(FVector2D(0.f, 0.f));
		BodyBubble->SetContent(BodyWords);

		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyBubble))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 14.f));

		// Hidden BodyText fallback (kept for back-compat with the C++ widget).
		UTextBlock* BodyText = New<UTextBlock>(Tree, TEXT("BodyText"));
		BodyText->SetFont(MakeRodin(14));
		BodyText->SetColorAndOpacity(FSlateColor(Cream));
		BodyText->SetAutoWrapText(true);
		BodyText->SetVisibility(ESlateVisibility::Collapsed);
		BodyText->SetText(FText::GetEmpty());
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyText))
			S->SetPadding(FMargin(0.f));

		// ── Divider line — collapsed in the bubble layout (bubbles imply
		//    grouping). Kept in the tree for back-compat.
		UBorder* Divider = New<UBorder>(Tree, TEXT("ChoicesDivider"));
		Divider->SetBrush(SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.0f)));
		Divider->SetPadding(FMargin(0.f));
		USizeBox* DivSize = New<USizeBox>(Tree, TEXT("ChoicesDividerSize"));
		DivSize->SetHeightOverride(1.f);
		DivSize->SetVisibility(ESlateVisibility::Collapsed);
		DivSize->AddChild(Divider);
		Column->AddChildToVerticalBox(DivSize);

		// ── ChoicesBox + 3 pre-built bubble-style buttons. ──
		UVerticalBox* ChoicesBox = New<UVerticalBox>(Tree, TEXT("ChoicesBox"));
		Column->AddChildToVerticalBox(ChoicesBox);

		auto BubbleBrush = [](float Alpha)
		{
			return RoundedBrush(
				FLinearColor(0.f, 0.f, 0.f, Alpha),
				FLinearColor(0.945f, 0.929f, 0.851f, 0.18f),
				1.f, 10.f);
		};

		for (int32 i = 0; i < 3; ++i)
		{
			const FString IdxStr = FString::FromInt(i);

			UButton* Btn = New<UButton>(Tree, FName(*FString::Printf(TEXT("ChoiceBtn_%d"), i)));
			FButtonStyle BtnStyle;
			BtnStyle.Normal   = BubbleBrush(0.55f);
			BtnStyle.Hovered  = BubbleBrush(0.72f);
			BtnStyle.Pressed  = BubbleBrush(0.85f);
			BtnStyle.Disabled = BubbleBrush(0.35f);
			Btn->SetStyle(BtnStyle);

			UHorizontalBox* Row = New<UHorizontalBox>(Tree,
				FName(*FString::Printf(TEXT("ChoiceRow_%d"), i)));

			// Circle number node (24×24)
			UBorder* CircleBg = New<UBorder>(Tree,
				FName(*FString::Printf(TEXT("ChoiceCircle_%d"), i)));
			CircleBg->SetBrush(RoundedBrush(
				FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),
				FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),
				1.f, 12.f));
			CircleBg->SetPadding(FMargin(0.f));
			CircleBg->SetHorizontalAlignment(HAlign_Center);
			CircleBg->SetVerticalAlignment(VAlign_Center);

			UTextBlock* CircleNum = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("ChoiceNum_%d"), i)));
			CircleNum->SetText(FText::AsNumber(i + 1));
			CircleNum->SetFont(MakeBMSPA(11));
			CircleNum->SetColorAndOpacity(FSlateColor(Cream));
			CircleNum->SetJustification(ETextJustify::Center);
			CircleBg->SetContent(CircleNum);

			USizeBox* CircleSize = New<USizeBox>(Tree,
				FName(*FString::Printf(TEXT("ChoiceCircleSize_%d"), i)));
			CircleSize->SetWidthOverride(24.f);
			CircleSize->SetHeightOverride(24.f);
			CircleSize->AddChild(CircleBg);

			if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(CircleSize))
			{
				HS->SetPadding(FMargin(0.f, 1.f, 10.f, 0.f));
				HS->SetVerticalAlignment(VAlign_Top);
			}

			UTextBlock* ChoiceLabel = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("ChoiceText_%d"), i)));
			ChoiceLabel->SetText(FText::FromString(FString::Printf(
				TEXT("Choice %d (placeholder — runtime sets actual text)"), i + 1)));
			ChoiceLabel->SetFont(MakeRodin(14));
			ChoiceLabel->SetColorAndOpacity(FSlateColor(Cream));
			ChoiceLabel->SetAutoWrapText(true);
			if (UHorizontalBoxSlot* HS = Row->AddChildToHorizontalBox(ChoiceLabel))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			Btn->SetContent(Row);
			if (UVerticalBoxSlot* VS = ChoicesBox->AddChildToVerticalBox(Btn))
			{
				VS->SetPadding(FMargin(0.f, 4.f));
			}
		}

		// ── Close button — same chalk-circle × as solid layout.
		UButton* CloseButton = New<UButton>(Tree, TEXT("CloseButton"));
		FButtonStyle CloseStyle;
		CloseStyle.Normal   = RoundedBrush(FLinearColor(0.031f, 0.035f, 0.047f, 0.6f),
		                                   FLinearColor(0.945f, 0.929f, 0.851f, 0.65f),
		                                   1.f, 14.f);
		CloseStyle.Hovered  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.08f),
		                                   FLinearColor::White, 1.f, 14.f);
		CloseStyle.Pressed  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f),
		                                   FLinearColor::White, 1.f, 14.f);
		CloseStyle.Disabled = CloseStyle.Normal;
		CloseButton->SetStyle(CloseStyle);

		UTextBlock* CloseLabel = New<UTextBlock>(Tree, TEXT("CloseLabel"));
		CloseLabel->SetText(FText::FromString(TEXT("×")));
		CloseLabel->SetFont(MakeBMSPA(16));
		CloseLabel->SetColorAndOpacity(FSlateColor(CreamDim));
		CloseLabel->SetJustification(ETextJustify::Center);
		CloseButton->SetContent(CloseLabel);

		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(CloseButton))
		{
			S->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
			S->SetAlignment(FVector2D(1.f, 0.f));
			S->SetPosition(FVector2D(-18.f, 14.f));
			S->SetSize(FVector2D(28.f, 28.f));
			S->SetZOrder(2);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  HUD WBP
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateHUDWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// No backdrop — bars sit directly on the canvas, top-left. Fixed-width
		// SizeBox gives the Fill-aligned rows/bars below a concrete width.
		// Keep in step with EclipseHUDWidget.cpp's ColumnWidth / BarHeight —
		// the populator and the runtime fallback must agree on dimensions.
		const float HudColumnWidth = 440.f;
		const float HudBarHeight   = 14.f;
		const int32 MeterMax    = 10;

		USizeBox* ColumnSize = New<USizeBox>(Tree, TEXT("MeterColumnSize"));
		ColumnSize->SetWidthOverride(HudColumnWidth);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(ColumnSize))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			S->SetAlignment(FVector2D(0.f, 0.f));
			S->SetAutoSize(true);
			S->SetPosition(FVector2D(20.f, 20.f));
		}

		UVerticalBox* MeterCol = New<UVerticalBox>(Tree, TEXT("MeterColumn"));
		ColumnSize->AddChild(MeterCol);

		auto BuildBar = [&](const TCHAR* Suffix, const FString& Label)
		{
			UVerticalBox* Block = New<UVerticalBox>(Tree,
				FName(*FString::Printf(TEXT("%sBlock"), Suffix)));

			// ── Top line: [Label] .......... [Value/Max] ──
			UHorizontalBox* TopRow = New<UHorizontalBox>(Tree,
				FName(*FString::Printf(TEXT("%sRow"), Suffix)));

			UTextBlock* LabelTxt = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("%sLabelText"), Suffix)));
			LabelTxt->SetText(FText::FromString(Label));
			LabelTxt->SetFont(MakeBMSPA(17, 2.f));
			LabelTxt->SetColorAndOpacity(FSlateColor(Cream));
			if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(LabelTxt))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Bottom);
			}

			UTextBlock* ValueTxt = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("%sValueText"), Suffix)));
			ValueTxt->SetText(FText::FromString(FString::Printf(TEXT("0/%d"), MeterMax)));
			ValueTxt->SetFont(MakeBMSPA(17, 1.5f));
			ValueTxt->SetColorAndOpacity(FSlateColor(Cream));
			ValueTxt->SetJustification(ETextJustify::Right);
			if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(ValueTxt))
			{
				HS->SetVerticalAlignment(VAlign_Bottom);
			}

			if (UVerticalBoxSlot* VS = Block->AddChildToVerticalBox(TopRow))
			{
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 3.f));
			}

			// ── Bottom line: full-width flat fill bar, no frame/track glow ──
			UProgressBar* Bar = New<UProgressBar>(Tree,
				FName(*FString::Printf(TEXT("%sBar"), Suffix)));
			{
				FProgressBarStyle Style;
				Style.BackgroundImage = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.3f));
				Style.FillImage       = SolidBrush(FLinearColor::White); // tinted per-frame via SetFillColorAndOpacity
				Bar->SetWidgetStyle(Style);
			}
			Bar->SetPercent(0.f);

			USizeBox* BarSize = New<USizeBox>(Tree,
				FName(*FString::Printf(TEXT("%sBarSize"), Suffix)));
			BarSize->SetHeightOverride(HudBarHeight);
			BarSize->AddChild(Bar);
			if (UVerticalBoxSlot* VS = Block->AddChildToVerticalBox(BarSize))
			{
				VS->SetHorizontalAlignment(HAlign_Fill);
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			}

			MeterCol->AddChildToVerticalBox(Block);
		};

		BuildBar(TEXT("Heat"),        TEXT("HEAT"));
		BuildBar(TEXT("Thirst"),      TEXT("THIRST"));
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Interact prompt WBP
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateInteractWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		UTextBlock* PromptText = New<UTextBlock>(Tree, TEXT("PromptText"));
		PromptText->SetFont(MakeBMSPA(28, 2.f));
		PromptText->SetColorAndOpacity(FSlateColor(Cyan));
		PromptText->SetJustification(ETextJustify::Center);
		PromptText->SetShadowOffset(FVector2D(0.f, 2.f));
		PromptText->SetShadowColorAndOpacity(FLinearColor(0.318f, 0.933f, 0.988f, 0.55f));
		PromptText->SetText(FText::FromString(TEXT("[E]  TALK TO ...")));

		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(PromptText))
		{
			S->SetAnchors(FAnchors(0.5f, 0.85f, 0.5f, 0.85f));
			S->SetAlignment(FVector2D(0.5f, 0.5f));
			S->SetAutoSize(true);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Pause-menu WBP — fullscreen dim + centred chalk panel + button list
//
//   ┌──────────────────────────────────────────┐
//   │              (dim 55% black)             │
//   │       ┌───────────────────────┐          │
//   │       │        PAUSED         │          │
//   │       │  ───────────────────  │          │
//   │       │      RESUME           │          │
//   │       │      SAVE             │          │
//   │       │      LOAD             │          │
//   │       │      MAIN MENU        │          │
//   │       │      QUIT             │          │
//   │       │     <status>          │          │
//   │       └───────────────────────┘          │
//   └──────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulatePauseMenuWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// Fullscreen dim
		UBorder* Dim = New<UBorder>(Tree, TEXT("Dim"));
		Dim->SetBrush(SolidBrush(FLinearColor::Black));   // fully opaque — no world showing through
		Dim->SetPadding(FMargin(0.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
			S->SetZOrder(0);
		}

		// Full-bleed chalk panel — Border content stretches full viewport so
		// the column hits both horizontal edges and the inner buttons can
		// span the whole width with HAlign_Fill on their slots.
		UBorder* Panel = New<UBorder>(Tree, TEXT("PausePanel"));
		Panel->SetBrush(SolidBrush(FLinearColor::Black));
		// Left-aligned column with a margin off the screen edge, rather
		// than a full-width centred block.
		Panel->SetPadding(FMargin(80.f, 0.f, 0.f, 0.f));
		Panel->SetHorizontalAlignment(HAlign_Left);
		Panel->SetVerticalAlignment(VAlign_Center);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
			S->SetZOrder(1);
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("PauseColumn"));
		Panel->SetContent(Column);

		// No heading — the menu is just the list of options.

		// MakeBtnIn — accepts an optional explicit label-widget name so the
		// slot rows can ship labels named "Slot0Label" etc. (matching the
		// BindWidgetOptional UPROPERTYs on the C++ class). Default falls
		// back to "<WidgetName>_Label" for non-slot buttons.
		auto MakeBtnIn = [&](UVerticalBox* Parent, const FString& Label, FName WidgetName,
			int32 FontSize = 22, FName LabelName = NAME_None)
		{
			UButton* Btn = New<UButton>(Tree, WidgetName);
			FButtonStyle BS;
			// Fully transparent in every state — the menu reads as plain
			// text, not buttons. The UButton is kept purely for click +
			// hover handling; nothing about it is drawn.
			BS.Normal   = SolidBrush(FLinearColor::Transparent);
			BS.Hovered  = SolidBrush(FLinearColor::Transparent);
			BS.Pressed  = SolidBrush(FLinearColor::Transparent);
			BS.Disabled = SolidBrush(FLinearColor::Transparent);
			Btn->SetStyle(BS);

			const FName ResolvedLabelName = LabelName.IsNone()
				? FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString()))
				: LabelName;
			UTextBlock* T = New<UTextBlock>(Tree, ResolvedLabelName);
			T->SetText(FText::FromString(Label));
			T->SetFont(MakeRodin(FontSize));
			T->SetColorAndOpacity(FSlateColor(Cream));
			T->SetJustification(ETextJustify::Left);
			Btn->SetContent(T);
			// Left, not Fill — the row hugs its text so the whole strip
			// isn't a click target and the list reads as a text column.
			if (UButtonSlot* BSlot = Cast<UButtonSlot>(T->Slot))
			{
				BSlot->SetHorizontalAlignment(HAlign_Left);
				BSlot->SetVerticalAlignment(VAlign_Center);
			}

			if (UVerticalBoxSlot* VS = Parent->AddChildToVerticalBox(Btn))
			{
				VS->SetPadding(FMargin(0.f, 6.f));
				VS->SetHorizontalAlignment(HAlign_Left);
			}
		};

		// MainList — Resume/Save/Load/MainMenu/Quit
		UVerticalBox* MainList = New<UVerticalBox>(Tree, TEXT("MainList"));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(MainList))
		{
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		// QUIT is wired to MainMenuBtn — it returns to the main menu rather
		// than exiting the application. The old app-quit row is gone.
		MakeBtnIn(MainList, TEXT("CONTINUE"), TEXT("ResumeBtn"));
		MakeBtnIn(MainList, TEXT("SAVE"),     TEXT("SaveBtn"));
		MakeBtnIn(MainList, TEXT("LOAD"),     TEXT("LoadBtn"));
		MakeBtnIn(MainList, TEXT("QUIT"),     TEXT("MainMenuBtn"));

		// SlotPicker — title + 3 slot rows + Back. Hidden by default; the
		// runtime widget toggles visibility when SAVE/LOAD is clicked.
		UVerticalBox* SlotPicker = New<UVerticalBox>(Tree, TEXT("SlotPicker"));
		SlotPicker->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(SlotPicker))
		{
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
		UTextBlock* SlotPickerTitle = New<UTextBlock>(Tree, TEXT("SlotPickerTitle"));
		SlotPickerTitle->SetText(FText::FromString(TEXT("SAVE GAME")));
		SlotPickerTitle->SetFont(MakeRodin(22));
		SlotPickerTitle->SetColorAndOpacity(FSlateColor(Cyan));
		SlotPickerTitle->SetJustification(ETextJustify::Left);
		if (UVerticalBoxSlot* VS = SlotPicker->AddChildToVerticalBox(SlotPickerTitle))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 32.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}
		// Labels keep the populator's natural "<WidgetName>_Label" naming —
		// the C++ UPROPERTYs Slot0Btn_Label / Slot1Btn_Label / Slot2Btn_Label
		// match this so BindWidgetOptional resolves without any post-rename.
		MakeBtnIn(SlotPicker, TEXT("SLOT 1  ·  EMPTY"), TEXT("Slot0Btn"), 20);
		MakeBtnIn(SlotPicker, TEXT("SLOT 2  ·  EMPTY"), TEXT("Slot1Btn"), 20);
		MakeBtnIn(SlotPicker, TEXT("SLOT 3  ·  EMPTY"), TEXT("Slot2Btn"), 20);
		MakeBtnIn(SlotPicker, TEXT("BACK"),             TEXT("SlotBackBtn"), 20);

		// Status line — reports save/load result. Lives below both sub-states.
		UTextBlock* StatusText = New<UTextBlock>(Tree, TEXT("StatusText"));
		StatusText->SetText(FText::GetEmpty());
		StatusText->SetFont(MakeRodin(18));
		StatusText->SetColorAndOpacity(FSlateColor(CreamDim));
		StatusText->SetJustification(ETextJustify::Left);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusText))
		{
			VS->SetPadding(FMargin(0.f, 48.f, 0.f, 0.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main-menu WBP — boot screen with NEW GAME / CONTINUE / QUIT.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateMainMenuWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		UBorder* Panel = New<UBorder>(Tree, TEXT("MainMenuPanel"));
		Panel->SetBrush(SolidBrush(FLinearColor(0.039f, 0.043f, 0.059f, 1.f)));
		// Left-aligned column with a margin off the screen edge, rather
		// than a full-width centred block.
		Panel->SetPadding(FMargin(80.f, 0.f, 0.f, 0.f));
		Panel->SetHorizontalAlignment(HAlign_Left);
		Panel->SetVerticalAlignment(VAlign_Center);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("MainMenuColumn"));
		Panel->SetContent(Column);

		// Big title
		UTextBlock* Title = New<UTextBlock>(Tree, TEXT("MainMenuTitle"));
		Title->SetText(FText::FromString(TEXT("ECLIPSE")));
		Title->SetFont(MakeBMSPA(200, 18.f));
		Title->SetColorAndOpacity(FSlateColor(Cyan));
		Title->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 96.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}

		auto MakeBtn = [&](const FString& Label, FName WidgetName)
		{
			UButton* Btn = New<UButton>(Tree, WidgetName);
			FButtonStyle BS;
			// Fully transparent in every state — the menu reads as plain
			// text, not buttons. The UButton is kept purely for click +
			// hover handling; nothing about it is drawn.
			BS.Normal   = SolidBrush(FLinearColor::Transparent);
			BS.Hovered  = SolidBrush(FLinearColor::Transparent);
			BS.Pressed  = SolidBrush(FLinearColor::Transparent);
			BS.Disabled = SolidBrush(FLinearColor::Transparent);
			Btn->SetStyle(BS);

			UTextBlock* T = New<UTextBlock>(Tree,
				FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
			T->SetText(FText::FromString(Label));
			T->SetFont(MakeRodin(56));
			T->SetColorAndOpacity(FSlateColor(Cream));
			T->SetJustification(ETextJustify::Center);
			Btn->SetContent(T);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Btn))
			{
				VS->SetPadding(FMargin(0.f, 16.f));
				VS->SetHorizontalAlignment(HAlign_Fill);
			}
		};
		MakeBtn(TEXT("NEW GAME"), TEXT("NewGameBtn"));
		MakeBtn(TEXT("CONTINUE"), TEXT("ContinueBtn"));
		MakeBtn(TEXT("QUIT"),     TEXT("QuitBtn"));

		UTextBlock* StatusText = New<UTextBlock>(Tree, TEXT("StatusText"));
		StatusText->SetText(FText::GetEmpty());
		StatusText->SetFont(MakeRodin(18));
		StatusText->SetColorAndOpacity(FSlateColor(CreamDim));
		StatusText->SetJustification(ETextJustify::Left);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusText))
		{
			VS->SetPadding(FMargin(0.f, 48.f, 0.f, 0.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory WBP — full-screen dim + centred chalk panel + held/equipped grid
//  + detail row + USE/DROP/CLOSE action buttons. Mirrors the runtime
//  fallback in UEclipseInventoryWidget::BuildFallbackTree so the asset can
//  be designer-restyled without giving up the named children that the C++
//  class binds via BindWidgetOptional.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateInventoryWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// Fullscreen dim under the panel
		UBorder* Dim = New<UBorder>(Tree, TEXT("Dim"));
		Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.65f)));
		Dim->SetPadding(FMargin(0.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}

		// Centred chalk panel — Disco Elysium-style slate slab
		UBorder* Panel = New<UBorder>(Tree, TEXT("InventoryPanel"));
		Panel->SetBrush(RoundedBrush(PaperWhite, LinkBlue, 1.f, 6.f));
		Panel->SetPadding(FMargin(36.f, 28.f));
		Panel->SetHorizontalAlignment(HAlign_Fill);
		Panel->SetVerticalAlignment(VAlign_Fill);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			S->SetAlignment(FVector2D(0.5f, 0.5f));
			// Sized to the paper doll alone — the AVAILABLE grid that used to
			// sit beside it is gone, so the panel lost its right half.
			S->SetSize(FVector2D(880.f, 860.f));
			S->SetZOrder(1);
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("InventoryColumn"));
		Panel->SetContent(Column);

		// ── The paper doll IS the inventory ───────────────────────────────
		// One screen: a silhouette with every slot arranged around it and
		// linked back to the body by a connector line. Six worn-clothing slots
		// plus HANDS and two POCKETS cells for loose items. Deliberately no tab
		// strip, no chip grid, no AVAILABLE pool — what you own is on your body
		// or it is not with you.
		{
			UHorizontalBox* WearablesPanel = New<UHorizontalBox>(Tree, TEXT("WearablesPanel"));
			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(WearablesPanel))
			{
				VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				VS->SetHorizontalAlignment(HAlign_Center);
			}

			// ── PaperdollCanvas (absolute-positioned children) ─────────
			UCanvasPanel* PaperdollCanvas = New<UCanvasPanel>(Tree, TEXT("PaperdollCanvas"));
			USizeBox* CanvasSize = New<USizeBox>(Tree, TEXT("PaperdollCanvasSize"));
			CanvasSize->SetHeightOverride(620.f);
			CanvasSize->SetWidthOverride(760.f);
			CanvasSize->AddChild(PaperdollCanvas);
			if (UHorizontalBoxSlot* HS = WearablesPanel->AddChildToHorizontalBox(CanvasSize))
			{
				HS->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
				HS->SetVerticalAlignment(VAlign_Top);
			}

			// Layout constants — silhouette anchored to canvas centre.
			const float CanvasW   = 760.f;
			const float SilhX     = 280.f;   // left edge of silhouette
			const float SilhY     = 30.f;
			const float SilhW     = 200.f;
			const float SilhH     = 540.f;
			const float SilhRightX = SilhX + SilhW;   // 480
			const float SlotW     = 140.f;
			const float SlotH     = 80.f;
			const float LeftSlotX = 40.f;             // right edge: 180
			const float RightSlotX = CanvasW - LeftSlotX - SlotW;  // 580
			const FLinearColor ConnectorTint(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.45f);

			// ── Silhouette ─────────────────────────────────────────────
			UBorder* Silhouette = New<UBorder>(Tree, TEXT("PaperdollSilhouette"));
			{
				FSlateBrush B;
				B.DrawAs    = ESlateBrushDrawType::RoundedBox;
				B.TintColor = FSlateColor(FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.06f));
				B.OutlineSettings.Color        = FSlateColor(LinkBlue);
				B.OutlineSettings.Width        = 1.5f;
				B.OutlineSettings.CornerRadii  = FVector4(4, 4, 4, 4);
				B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
				Silhouette->SetBrush(B);
			}
			Silhouette->SetHorizontalAlignment(HAlign_Center);
			Silhouette->SetVerticalAlignment(VAlign_Center);
			UTextBlock* YouLabel = New<UTextBlock>(Tree, TEXT("PaperdollYouLabel"));
			YouLabel->SetText(FText::FromString(TEXT("YOU")));
			YouLabel->SetFont(MakeRodin(28));
			YouLabel->SetColorAndOpacity(FSlateColor(EclipseUI::LinkBlueDim));
			YouLabel->SetJustification(ETextJustify::Center);
			Silhouette->SetContent(YouLabel);
			if (UCanvasPanelSlot* CS = PaperdollCanvas->AddChildToCanvas(Silhouette))
			{
				CS->SetPosition(FVector2D(SilhX, SilhY));
				CS->SetSize(FVector2D(SilhW, SilhH));
			}

			// ── Slot positions (y) keyed to body region on the silhouette ──
			struct FSlotSpec
			{
				const TCHAR*       Name;
				EEclipseSlotType   Type;
				bool               bLeftSide;
				float              SlotY;
				float              BodyY;   // y on silhouette where connector terminates
			};
			// y-positions and body-region targets spread across the
			// silhouette top (head/eyes) → neck/torso → waist/feet.
			const FSlotSpec Specs[] = {
				{ TEXT("HeadSlot"),    EEclipseSlotType::Head,    true,  30.f,  70.f  },
				{ TEXT("EyesSlot"),    EEclipseSlotType::Eyes,    false, 30.f,  110.f },
				{ TEXT("NeckSlot"),    EEclipseSlotType::Neck,    true,  150.f, 180.f },
				{ TEXT("TopSlot"),     EEclipseSlotType::Top,     false, 150.f, 260.f },
				{ TEXT("HandsSlot"),   EEclipseSlotType::Hands,   true,  270.f, 300.f },
				{ TEXT("BottomSlot"),  EEclipseSlotType::Bottom,  false, 270.f, 410.f },
				// Two cells for one 2-capacity carrier; the runtime widgets differ
				// only by CellIndex.
				{ TEXT("Pocket0Slot"), EEclipseSlotType::Pockets, true,  390.f, 430.f },
				{ TEXT("Pocket1Slot"), EEclipseSlotType::Pockets, true,  480.f, 455.f },
				{ TEXT("ShoesSlot"),   EEclipseSlotType::Shoes,   false, 390.f, 530.f },
			};

			for (const FSlotSpec& Spec : Specs)
			{
				const float SlotX = Spec.bLeftSide ? LeftSlotX : RightSlotX;

				// Empty mount placeholder — the slot widget itself is
				// created at RUNTIME via CreateWidget (see NativeConstruct)
				// and inserted here. Embedding a UEclipseClothingSlotWidget
				// directly in the populator's tree renders an invisible
				// SSpacer because a ConstructWidget'd C++ UserWidget has a
				// null RootWidget. Named "<Slot>Mount" so the inventory
				// widget can find it. A faint frame keeps the empty slot
				// readable even before runtime fills it.
				UBorder* Mount = New<UBorder>(Tree,
					FName(*FString::Printf(TEXT("%sMount"), Spec.Name)));
				{
					FSlateBrush B;
					B.DrawAs    = ESlateBrushDrawType::RoundedBox;
					B.TintColor = FSlateColor(FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.05f));
					B.OutlineSettings.Color        = FSlateColor(LinkBlueDim);
					B.OutlineSettings.Width        = 1.f;
					B.OutlineSettings.CornerRadii  = FVector4(3, 3, 3, 3);
					B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
					Mount->SetBrush(B);
				}
				Mount->SetPadding(FMargin(0.f));
				Mount->SetHorizontalAlignment(HAlign_Fill);
				Mount->SetVerticalAlignment(VAlign_Fill);
				if (UCanvasPanelSlot* CS = PaperdollCanvas->AddChildToCanvas(Mount))
				{
					CS->SetPosition(FVector2D(SlotX, Spec.SlotY));
					CS->SetSize(FVector2D(SlotW, SlotH));
				}

				// Connector line — thin cyan stripe from slot edge to body.
				const float SlotMidY = Spec.SlotY + SlotH * 0.5f;
				const float LineLeftX  = Spec.bLeftSide ? (SlotX + SlotW) : SilhRightX;
				const float LineRightX = Spec.bLeftSide ? SilhX           : RightSlotX;
				const float LineW = LineRightX - LineLeftX;

				UBorder* Line = New<UBorder>(Tree,
					FName(*FString::Printf(TEXT("%sLine"), Spec.Name)));
				Line->SetBrush(SolidBrush(ConnectorTint));
				Line->SetPadding(FMargin(0.f));
				if (UCanvasPanelSlot* CS = PaperdollCanvas->AddChildToCanvas(Line))
				{
					CS->SetPosition(FVector2D(LineLeftX, SlotMidY));
					CS->SetSize(FVector2D(LineW, 1.f));
					CS->SetZOrder(-1);   // behind silhouette/slots
				}

				// Vertical drop to body terminus — visual cue that the
				// line plugs into the relevant body region.
				const float DropTopY    = FMath::Min(SlotMidY, Spec.BodyY);
				const float DropBottomY = FMath::Max(SlotMidY, Spec.BodyY);
				const float DropH       = FMath::Max(1.f, DropBottomY - DropTopY);
				const float DropX       = Spec.bLeftSide ? SilhX : SilhRightX;

				UBorder* Drop = New<UBorder>(Tree,
					FName(*FString::Printf(TEXT("%sDrop"), Spec.Name)));
				Drop->SetBrush(SolidBrush(ConnectorTint));
				Drop->SetPadding(FMargin(0.f));
				if (UCanvasPanelSlot* CS = PaperdollCanvas->AddChildToCanvas(Drop))
				{
					CS->SetPosition(FVector2D(DropX, DropTopY));
					CS->SetSize(FVector2D(1.f, DropH));
					CS->SetZOrder(-1);
				}
			}

		}

		// ── Detail panel ─────────────────────────────────────────────────
		UVerticalBox* DetailPanel = New<UVerticalBox>(Tree, TEXT("DetailPanel"));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DetailPanel))
			VS->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));

		UTextBlock* SelectedNameText = New<UTextBlock>(Tree, TEXT("SelectedNameText"));
		SelectedNameText->SetText(FText::FromString(TEXT("(select an item)")));
		SelectedNameText->SetFont(MakeRodin(20));
		SelectedNameText->SetColorAndOpacity(FSlateColor(LinkBlue));
		if (UVerticalBoxSlot* VS = DetailPanel->AddChildToVerticalBox(SelectedNameText))
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		UTextBlock* SelectedDescText = New<UTextBlock>(Tree, TEXT("SelectedDescText"));
		SelectedDescText->SetText(FText::FromString(TEXT("Click something on the body to inspect it.")));
		SelectedDescText->SetColorAndOpacity(FSlateColor(LinkBlueDim));
		SelectedDescText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* VS = DetailPanel->AddChildToVerticalBox(SelectedDescText))
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

		// ── Action row ───────────────────────────────────────────────────
		UHorizontalBox* Actions = New<UHorizontalBox>(Tree, TEXT("ActionRow"));
		DetailPanel->AddChildToVerticalBox(Actions);

		auto MakeBtn = [&](const FString& Label, FName WidgetName)
		{
			UButton* Btn = New<UButton>(Tree, WidgetName);
			FButtonStyle BS;
			// Fully transparent in every state — the menu reads as plain
			// text, not buttons. The UButton is kept purely for click +
			// hover handling; nothing about it is drawn.
			BS.Normal   = SolidBrush(FLinearColor::Transparent);
			BS.Hovered  = SolidBrush(FLinearColor::Transparent);
			BS.Pressed  = SolidBrush(FLinearColor::Transparent);
			BS.Disabled = SolidBrush(FLinearColor::Transparent);
			Btn->SetStyle(BS);

			UTextBlock* T = New<UTextBlock>(Tree, FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
			T->SetText(FText::FromString(Label));
			T->SetColorAndOpacity(FSlateColor(LinkBlue));
			T->SetJustification(ETextJustify::Center);
			Btn->SetContent(T);

			if (UHorizontalBoxSlot* HS = Actions->AddChildToHorizontalBox(Btn))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(4.f, 8.f));
			}
		};

		MakeBtn(TEXT("USE"),    TEXT("UseBtn"));
		MakeBtn(TEXT("CLOSE"),  TEXT("CloseBtn"));
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Swap prompt WBP — "your hands are full, what do you want to leave?"
//
//  Frame only. CandidateRow is deliberately shipped EMPTY: the number of
//  side-by-side boxes depends on what the player happens to be carrying when
//  the prompt fires, so UEclipseSwapPromptWidget::BuildCandidateBoxes fills
//  it at runtime. Widget names match that class's BindWidgetOptional list —
//  Title, IncomingLabel, CandidateRow, CancelBtn.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateSwapPromptWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// No dim layer: the prompt doesn't pause, so darkening the screen
		// would promise a stop that isn't happening.
		UBorder* Panel = New<UBorder>(Tree, TEXT("SwapPanel"));
		Panel->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
		Panel->SetPadding(FMargin(18.f, 14.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
			S->SetAlignment(FVector2D(0.5f, 1.f));
			S->SetPosition(FVector2D(0.f, -140.f));
			S->SetSize(FVector2D(560.f, 120.f));
			S->SetZOrder(1);
		}

		// Shipped EMPTY on purpose — UEclipseSwapPromptWidget::BuildCandidateBoxes
		// fills it with [old] -> [new] at runtime, since both depend on what
		// the player is carrying at that moment.
		UHorizontalBox* Row = New<UHorizontalBox>(Tree, TEXT("CandidateRow"));
		Panel->SetContent(Row);
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Stats menu WBP — modal centred overlay in the HUD's visual language.
//
//  Same widget-name conventions as the C++ widget's BindWidgetOptional list
//  so once the WBP is populated, the C++ side rebinds without any rename
//  step. Names: AestheticsRow, RhythmRow, ZenRow,
//  PsychedelicsRow, HeatRow, ThirstRow, CurrencyRow, CloseBtn. (The widget
//  reads stat values into these UTextBlocks at RefreshAll-time.)
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulateStatsMenuWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// Fullscreen dim beneath the panel (matches Inventory / Pause).
		UBorder* Dim = New<UBorder>(Tree, TEXT("Dim"));
		Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.65f)));
		Dim->SetPadding(FMargin(0.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}

		// Centred navy panel — same RoundedBrush(PanelBg, PanelBorder) as the
		// HUD background so the modal reads as "HUD expanded full-screen".
		UBorder* Panel = New<UBorder>(Tree, TEXT("StatsPanel"));
		Panel->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
		Panel->SetPadding(FMargin(28.f, 22.f));
		Panel->SetHorizontalAlignment(HAlign_Fill);
		Panel->SetVerticalAlignment(VAlign_Fill);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			S->SetAlignment(FVector2D(0.5f, 0.5f));
			S->SetSize(FVector2D(720.f, 560.f));
			S->SetZOrder(1);
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("StatsColumn"));
		Panel->SetContent(Column);

		// ── Title — Berenjena caps, cyan, generous letter spacing ────────────
		UTextBlock* Title = New<UTextBlock>(Tree, TEXT("StatsTitle"));
		Title->SetText(FText::FromString(TEXT("STATS")));
		Title->SetFont(MakeBerenjena(44, 8.f));
		Title->SetColorAndOpacity(FSlateColor(Cyan));
		Title->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 20.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}

		// ── Top row: Portrait | Stat list ────────────────────────────────────
		UHorizontalBox* TopRow = New<UHorizontalBox>(Tree, TEXT("TopRow"));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(TopRow))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}

		// Portrait — HUD-style 120×160 navy frame with cyan outline + "YOU".
		// Same construction as PopulateHUDWBP::PortraitFrame, just bigger.
		UBorder* PortraitFrame = New<UBorder>(Tree, TEXT("PortraitFrame"));
		{
			FSlateBrush PB;
			PB.DrawAs    = ESlateBrushDrawType::RoundedBox;
			PB.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
			PB.OutlineSettings.Color        = FSlateColor(Cyan);
			PB.OutlineSettings.Width        = 2.f;
			PB.OutlineSettings.CornerRadii  = FVector4(0,0,0,0);
			PB.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			PortraitFrame->SetBrush(PB);
		}
		PortraitFrame->SetHorizontalAlignment(HAlign_Center);
		PortraitFrame->SetVerticalAlignment(VAlign_Center);

		UTextBlock* PortraitLabel = New<UTextBlock>(Tree, TEXT("PortraitLabel"));
		PortraitLabel->SetText(FText::FromString(TEXT("YOU")));
		PortraitLabel->SetFont(MakeBerenjena(20, 5.f));
		PortraitLabel->SetColorAndOpacity(FSlateColor(Cyan));
		PortraitLabel->SetJustification(ETextJustify::Center);
		PortraitFrame->SetContent(PortraitLabel);

		USizeBox* PortraitSize = New<USizeBox>(Tree, TEXT("PortraitSize"));
		PortraitSize->SetWidthOverride(120.f);
		PortraitSize->SetHeightOverride(160.f);
		PortraitSize->AddChild(PortraitFrame);

		if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(PortraitSize))
		{
			HS->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
			HS->SetVerticalAlignment(VAlign_Top);
		}

		// Stat list — 5 rows, each "LABEL     N" via SetText at runtime.
		// One TextBlock per row keeps designer styling on a single row entity.
		UVerticalBox* StatList = New<UVerticalBox>(Tree, TEXT("StatList"));
		if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(StatList))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetVerticalAlignment(VAlign_Top);
		}

		auto MakeStatRow = [&](FName WidgetName, const FString& DefaultLabel)
		{
			UTextBlock* T = New<UTextBlock>(Tree, WidgetName);
			T->SetText(FText::FromString(DefaultLabel));
			T->SetFont(MakeBerenjena(20, 4.f));
			T->SetColorAndOpacity(FSlateColor(Cream));
			if (UVerticalBoxSlot* VS = StatList->AddChildToVerticalBox(T))
			{
				VS->SetPadding(FMargin(0.f, 4.f));
			}
		};

		MakeStatRow(TEXT("AestheticsRow"),   TEXT("AESTHETICS     1"));
		MakeStatRow(TEXT("RhythmRow"),       TEXT("RHYTHM         1"));
		MakeStatRow(TEXT("ZenRow"),          TEXT("ZEN            1"));
		MakeStatRow(TEXT("PsychedelicsRow"), TEXT("PSYCHEDELICS   1"));

		// (Heat / Thirst meters and currency readout intentionally omitted —
		// stats panel is for the 5 character stats only. Heat/Thirst already
		// surface on the persistent HUD; currency belongs to the HUD too.)

		// ── Close button ─────────────────────────────────────────────────────
		UButton* CloseBtn = New<UButton>(Tree, TEXT("CloseBtn"));
		{
			FButtonStyle BS;
			// Fully transparent in every state — the menu reads as plain
			// text, not buttons. The UButton is kept purely for click +
			// hover handling; nothing about it is drawn.
			BS.Normal   = SolidBrush(FLinearColor::Transparent);
			BS.Hovered  = SolidBrush(FLinearColor::Transparent);
			BS.Pressed  = SolidBrush(FLinearColor::Transparent);
			BS.Disabled = SolidBrush(FLinearColor::Transparent);
			CloseBtn->SetStyle(BS);
		}
		UTextBlock* CloseLabel = New<UTextBlock>(Tree, TEXT("CloseBtn_Label"));
		CloseLabel->SetText(FText::FromString(TEXT("CLOSE")));
		CloseLabel->SetFont(MakeBerenjena(18, 4.f));
		CloseLabel->SetColorAndOpacity(FSlateColor(Cream));
		CloseLabel->SetJustification(ETextJustify::Center);
		CloseBtn->SetContent(CloseLabel);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(CloseBtn))
		{
			VS->SetPadding(FMargin(0.f, 18.f, 0.f, 0.f));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phone WBP — left-edge 280×460 panel, ports the prototype's #phone-ui
//  block. Widget names match UEclipsePhoneWidget's BindWidgetOptional fields
//  so the WBP becomes designer-editable while the C++ class continues to
//  drive logic (clock, wallet, tab swap).
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseUiBuilder::PopulatePhoneWBP(const FString& WBPAssetPath)
{
#if WITH_EDITOR
	using namespace EclipseUI;
	return DoBuild(WBPAssetPath, [](UWidgetBlueprint* WBP, UWidgetTree* Tree)
	{
		UCanvasPanel* Root = New<UCanvasPanel>(Tree, TEXT("Canvas_0"));
		Tree->RootWidget = Root;

		// ── Outer phone panel — navy gradient + cyan trim ───────────────
		UBorder* Panel = New<UBorder>(Tree, TEXT("PhonePanel"));
		Panel->SetBrush(RoundedBrush(
			FLinearColor(0.039f, 0.043f, 0.078f, 0.97f),
			FLinearColor(0.318f, 0.933f, 0.988f, 0.55f),
			1.5f, 10.f));
		Panel->SetPadding(FMargin(16.f, 14.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			// Left-edge, vertically centred, 280×460.
			S->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
			S->SetAlignment(FVector2D(0.f, 0.5f));
			S->SetPosition(FVector2D(24.f, 0.f));
			S->SetSize(FVector2D(280.f, 460.f));
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("PhoneColumn"));
		Panel->SetContent(Column);

		// ── Header strip: "PHONE" + close × ─────────────────────────────
		{
			UHorizontalBox* HeaderRow = New<UHorizontalBox>(Tree, TEXT("PhoneHeaderRow"));

			UTextBlock* Title = New<UTextBlock>(Tree, TEXT("PhoneTitle"));
			Title->SetText(FText::FromString(TEXT("PHONE")));
			Title->SetFont(MakeBMSPA(18, 3.f));
			Title->SetColorAndOpacity(FSlateColor(Cyan));
			if (UHorizontalBoxSlot* HS = HeaderRow->AddChildToHorizontalBox(Title))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			UButton* CloseBtn = New<UButton>(Tree, TEXT("CloseBtn"));
			FButtonStyle CloseStyle;
			CloseStyle.Normal   = RoundedBrush(FLinearColor(0.031f, 0.035f, 0.063f, 0.6f),
			                                   FLinearColor(0.945f, 0.929f, 0.851f, 0.65f),
			                                   1.f, 12.f);
			CloseStyle.Hovered  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.10f),
			                                   FLinearColor::White, 1.f, 12.f);
			CloseStyle.Pressed  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.18f),
			                                   FLinearColor::White, 1.f, 12.f);
			CloseStyle.Disabled = CloseStyle.Normal;
			CloseBtn->SetStyle(CloseStyle);
			UTextBlock* CloseLabel = New<UTextBlock>(Tree, TEXT("CloseLabel"));
			CloseLabel->SetText(FText::FromString(TEXT("x")));
			CloseLabel->SetFont(MakeBMSPA(16));
			CloseLabel->SetColorAndOpacity(FSlateColor(CreamDim));
			CloseLabel->SetJustification(ETextJustify::Center);
			CloseBtn->SetContent(CloseLabel);
			USizeBox* CloseSize = New<USizeBox>(Tree, TEXT("CloseBtnSize"));
			CloseSize->SetWidthOverride(24.f);
			CloseSize->SetHeightOverride(24.f);
			CloseSize->AddChild(CloseBtn);
			if (UHorizontalBoxSlot* HS = HeaderRow->AddChildToHorizontalBox(CloseSize))
				HS->SetVerticalAlignment(VAlign_Center);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(HeaderRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// Header divider
		{
			UBorder* Div = New<UBorder>(Tree, TEXT("PhoneHeaderDiv"));
			Div->SetBrush(SolidBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.40f)));
			USizeBox* DivSize = New<USizeBox>(Tree, TEXT("PhoneHeaderDivSize"));
			DivSize->SetHeightOverride(1.f);
			DivSize->AddChild(Div);
			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DivSize))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── Status row: clock + chapter (left) | wallet (right) ─────────
		{
			UHorizontalBox* StatusRow = New<UHorizontalBox>(Tree, TEXT("PhoneStatusRow"));

			UVerticalBox* ClockCol = New<UVerticalBox>(Tree, TEXT("PhoneClockCol"));

			UTextBlock* ClockT = New<UTextBlock>(Tree, TEXT("ClockText"));
			ClockT->SetText(FText::FromString(TEXT("0:00")));
			ClockT->SetFont(MakeBMSPA(26, 2.f));
			ClockT->SetColorAndOpacity(FSlateColor(Cream));
			ClockCol->AddChildToVerticalBox(ClockT);

			UTextBlock* ChapT = New<UTextBlock>(Tree, TEXT("ChapterLabelText"));
			ChapT->SetText(FText::FromString(TEXT("CH 0")));
			ChapT->SetFont(MakeBMSPA(11, 2.f));
			ChapT->SetColorAndOpacity(FSlateColor(CreamDim));
			ClockCol->AddChildToVerticalBox(ChapT);

			if (UHorizontalBoxSlot* HS = StatusRow->AddChildToHorizontalBox(ClockCol))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			UTextBlock* WalletT = New<UTextBlock>(Tree, TEXT("WalletText"));
			WalletT->SetText(FText::FromString(TEXT("C 0   N 0")));
			WalletT->SetFont(MakeBMSPA(14, 2.f));
			WalletT->SetColorAndOpacity(FSlateColor(Cyan));
			WalletT->SetJustification(ETextJustify::Right);
			if (UHorizontalBoxSlot* HS = StatusRow->AddChildToHorizontalBox(WalletT))
				HS->SetVerticalAlignment(VAlign_Center);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		}

		// ── Tab row: CONTACTS / NOTES ───────────────────────────────────
		{
			UHorizontalBox* TabRow = New<UHorizontalBox>(Tree, TEXT("PhoneTabRow"));

			auto MakeTabBtn = [&](FName Name, const FString& Label) -> UButton*
			{
				UButton* B = New<UButton>(Tree, Name);
				FButtonStyle BS;
				BS.Normal   = RoundedBrush(FLinearColor(0.039f, 0.043f, 0.078f, 0.60f),
				                           FLinearColor(0.318f, 0.933f, 0.988f, 0.35f),
				                           1.f, 4.f);
				BS.Hovered  = RoundedBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.18f),
				                           FLinearColor(0.318f, 0.933f, 0.988f, 0.85f),
				                           1.f, 4.f);
				BS.Pressed  = RoundedBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.32f),
				                           FLinearColor::White, 1.f, 4.f);
				BS.Disabled = BS.Normal;
				B->SetStyle(BS);
				UTextBlock* Lbl = New<UTextBlock>(Tree, NAME_None);
				Lbl->SetText(FText::FromString(Label));
				Lbl->SetFont(MakeBMSPA(12, 3.f));
				Lbl->SetColorAndOpacity(FSlateColor(Cream));
				Lbl->SetJustification(ETextJustify::Center);
				B->SetContent(Lbl);
				return B;
			};

			UButton* Contacts = MakeTabBtn(TEXT("ContactsTabBtn"), TEXT("CONTACTS"));
			UButton* Notes    = MakeTabBtn(TEXT("NotesTabBtn"),    TEXT("NOTES"));

			if (UHorizontalBoxSlot* HS = TabRow->AddChildToHorizontalBox(Contacts))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			if (UHorizontalBoxSlot* HS = TabRow->AddChildToHorizontalBox(Notes))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(TabRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── Scrolling content area ──────────────────────────────────────
		{
			UScrollBox* Scroll = New<UScrollBox>(Tree, TEXT("ContentScroll"));
			Scroll->SetAnimateWheelScrolling(true);

			UTextBlock* Placeholder = New<UTextBlock>(Tree, TEXT("ContentPlaceholder"));
			Placeholder->SetText(FText::FromString(
				TEXT("No contacts yet.\n\nCharacters you meet will appear here.")));
			Placeholder->SetFont(MakeRodin(13));
			Placeholder->SetColorAndOpacity(FSlateColor(CreamDim));
			Placeholder->SetAutoWrapText(true);
			Scroll->AddChild(Placeholder);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Scroll))
			{
				VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			}
		}

		// Footer divider
		{
			UBorder* Div = New<UBorder>(Tree, TEXT("PhoneFooterDiv"));
			Div->SetBrush(SolidBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.40f)));
			USizeBox* DivSize = New<USizeBox>(Tree, TEXT("PhoneFooterDivSize"));
			DivSize->SetHeightOverride(1.f);
			DivSize->AddChild(Div);
			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DivSize))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── Action row: CALL · TEXT (disabled stubs) ────────────────────
		{
			UHorizontalBox* ActionRow = New<UHorizontalBox>(Tree, TEXT("PhoneActionRow"));

			auto MakeActionBtn = [&](FName Name, const FString& Label) -> UButton*
			{
				UButton* B = New<UButton>(Tree, Name);
				FButtonStyle BS;
				BS.Normal   = RoundedBrush(FLinearColor(0.039f, 0.043f, 0.078f, 0.45f),
				                           FLinearColor(0.945f, 0.929f, 0.851f, 0.25f),
				                           1.f, 4.f);
				BS.Hovered  = BS.Normal;
				BS.Pressed  = BS.Normal;
				BS.Disabled = BS.Normal;
				B->SetStyle(BS);
				B->SetIsEnabled(false);
				UTextBlock* Lbl = New<UTextBlock>(Tree, NAME_None);
				Lbl->SetText(FText::FromString(Label));
				Lbl->SetFont(MakeBMSPA(12, 3.f));
				Lbl->SetColorAndOpacity(FSlateColor(CreamDim));
				Lbl->SetJustification(ETextJustify::Center);
				B->SetContent(Lbl);
				return B;
			};

			UButton* Call = MakeActionBtn(TEXT("CallBtn"), TEXT("CALL"));
			UButton* Text = MakeActionBtn(TEXT("TextBtn"), TEXT("TEXT"));

			if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(Call))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(Text))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}

			Column->AddChildToVerticalBox(ActionRow);
		}
	});
#else
	(void)WBPAssetPath; return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build a UFont composite that wraps a UFontFace
// ─────────────────────────────────────────────────────────────────────────────

UFont* UEclipseUiBuilder::BuildFontComposite(UFontFace* FontFace,
	const FString& PackagePath, const FString& AssetName)
{
#if WITH_EDITOR
	if (!FontFace)
	{
		UE_LOG(LogEclipse, Error, TEXT("BuildFontComposite: null FontFace"));
		return nullptr;
	}

	// Create or load the package
	const FString FullPackagePath = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullPackagePath);
	Package->FullyLoad();

	// Find existing or create a new UFont
	UFont* Font = FindObject<UFont>(Package, *AssetName);
	const bool bWasCreated = (Font == nullptr);
	if (!Font)
	{
		Font = NewObject<UFont>(Package, *AssetName, RF_Public | RF_Standalone);
	}

	// Build the composite: one typeface entry "Default" pointing at our face.
	FFontData FaceData(FontFace);

	FTypefaceEntry Entry;
	Entry.Name = TEXT("Default");
	Entry.Font = FaceData;

	Font->CompositeFont.DefaultTypeface.Fonts.Empty();
	Font->CompositeFont.DefaultTypeface.Fonts.Add(Entry);
	Font->CompositeFont.SubTypefaces.Empty();
	Font->CompositeFont.FallbackTypeface = FCompositeFallbackFont();
	Font->FontCacheType = EFontCacheType::Runtime;

	if (bWasCreated)
	{
		FAssetRegistryModule::AssetCreated(Font);
	}
	Font->MarkPackageDirty();
	Package->MarkPackageDirty();

	UE_LOG(LogEclipse, Log, TEXT("BuildFontComposite: %s '%s' wrapping FontFace '%s'"),
		bWasCreated ? TEXT("created") : TEXT("updated"),
		*FullPackagePath, *FontFace->GetName());
	return Font;
#else
	(void)FontFace; (void)PackagePath; (void)AssetName;
	return nullptr;
#endif
}
