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
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Engine/Engine.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

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

		// ── Dialogue panel — right-anchored 480px wide ──
		// To fake a radial fade-from-edges (Slate has no native gradient brush)
		// we stack multiple inset rounded-black layers in a UOverlay. Each
		// successive layer is inset further and uses a higher alpha, so the
		// visible edge is faint and the centre is a near-solid black panel
		// with rounded corners.
		UOverlay* Panel = New<UOverlay>(Tree, TEXT("DialoguePanel"));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
			S->SetAlignment(FVector2D(1.f, 0.f));
			S->SetPosition(FVector2D(0.f, 0.f));
			S->SetSize(FVector2D(480.f, 0.f));
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

		// ── Speaker portrait — sticks off the LEFT edge of the panel, ──
		// ── slightly overlapping. Texture is set per-conversation by the C++ ──
		// ── handler reading the active NPC's PortraitTexture. ──
		// Portrait photo aspect (~5:7 → 220×308). Anchored to right edge of
		// viewport, at vertical center. Position from right edge:
		//   panel_left = -480
		//   overlap = 30 (portrait right edge sits 30px inside panel left)
		//   portrait_right = panel_left + overlap = -450
		//   portrait_left  = portrait_right - 220 = -670
		UImage* SpeakerPortrait = New<UImage>(Tree, TEXT("SpeakerPortrait"));
		FSlateBrush PortraitBrush;
		PortraitBrush.DrawAs    = ESlateBrushDrawType::RoundedBox;
		PortraitBrush.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
		PortraitBrush.OutlineSettings.CornerRadii  = FVector4(6, 6, 6, 6);
		PortraitBrush.OutlineSettings.Color        = FSlateColor(Cyan);
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
			S->SetAnchors(FAnchors(1.f, 0.5f, 1.f, 0.5f));   // right edge, vertical center
			S->SetAlignment(FVector2D(0.f, 0.5f));            // left-middle of portrait at anchor
			S->SetPosition(FVector2D(-670.f, 0.f));
			S->SetSize(FVector2D(220.f, 308.f));
			S->SetZOrder(3);                                  // above the panel fade stack
		}

		// Speaker name (BMSPA, cyan)
		UTextBlock* SpeakerNameText = New<UTextBlock>(Tree, TEXT("SpeakerNameText"));
		SpeakerNameText->SetFont(MakeBMSPA(22, 3.f));
		SpeakerNameText->SetColorAndOpacity(FSlateColor(Cyan));
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
		BodyText->SetFont(MakeRodin(18));
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
			ChoiceLabel->SetFont(MakeRodin(15));
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

		// Close button (chalk circle ×)
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

		// (Centre crosshair removed — point-and-click interactions don't need
		// a reticle and the cursor itself is now the aim indicator.)

		// HUD container — bottom-right, navy panel
		UBorder* HudBg = New<UBorder>(Tree, TEXT("HudBg"));
		HudBg->SetBrush(RoundedBrush(PanelBg, PanelBorder, 1.f, 0.f));
		HudBg->SetPadding(FMargin(12.f, 10.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(HudBg))
		{
			S->SetAnchors(FAnchors(1.f, 1.f, 1.f, 1.f));
			S->SetAlignment(FVector2D(1.f, 1.f));
			S->SetAutoSize(true);
			S->SetPosition(FVector2D(-20.f, -20.f));
		}

		// HudBg holds a vertical column: [InventoryRibbon] above [HudRow].
		// The runtime widget rebuilds chip contents per-tick from game state
		// — the populator just creates the named container so designers can
		// re-skin it in the WBP if they want.
		UVerticalBox* HudColumn = New<UVerticalBox>(Tree, TEXT("HudColumn"));
		HudBg->SetContent(HudColumn);

		UHorizontalBox* InventoryRibbon = New<UHorizontalBox>(Tree, TEXT("InventoryRibbon"));
		USizeBox* InvSize = New<USizeBox>(Tree, TEXT("InventoryRibbonSize"));
		InvSize->SetMinDesiredHeight(22.f);
		InvSize->AddChild(InventoryRibbon);
		if (UVerticalBoxSlot* VS = HudColumn->AddChildToVerticalBox(InvSize))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
			VS->SetHorizontalAlignment(HAlign_Right);
		}

		UHorizontalBox* Row = New<UHorizontalBox>(Tree, TEXT("HudRow"));
		HudColumn->AddChildToVerticalBox(Row);

		// Portrait
		UBorder* PortraitFrame = New<UBorder>(Tree, TEXT("PortraitFrame"));
		FSlateBrush PB;
		PB.DrawAs    = ESlateBrushDrawType::RoundedBox;
		PB.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
		PB.OutlineSettings.Color        = FSlateColor(Cyan);
		PB.OutlineSettings.Width        = 2.f;
		PB.OutlineSettings.CornerRadii  = FVector4(0,0,0,0);
		PB.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
		PortraitFrame->SetBrush(PB);

		USizeBox* PortraitSize = New<USizeBox>(Tree, TEXT("PortraitSize"));
		PortraitSize->SetWidthOverride(90.f);
		PortraitSize->SetHeightOverride(112.f);
		PortraitSize->AddChild(PortraitFrame);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(PortraitSize))
		{
			S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			S->SetVerticalAlignment(VAlign_Center);
		}

		UTextBlock* PortraitLabel = New<UTextBlock>(Tree, TEXT("PortraitLabel"));
		PortraitLabel->SetText(FText::FromString(TEXT("YOU")));
		PortraitLabel->SetFont(MakeBMSPA(14, 4.f));
		PortraitLabel->SetColorAndOpacity(FSlateColor(Cyan));
		PortraitLabel->SetJustification(ETextJustify::Center);
		PortraitFrame->SetContent(PortraitLabel);
		PortraitFrame->SetHorizontalAlignment(HAlign_Center);
		PortraitFrame->SetVerticalAlignment(VAlign_Center);

		// Heat cluster
		UVerticalBox* HeatCluster = New<UVerticalBox>(Tree, TEXT("HeatCluster"));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(HeatCluster))
		{
			S->SetPadding(FMargin(0.f, 0.f, 12.f, 0.f));
			S->SetVerticalAlignment(VAlign_Bottom);
		}
		UTextBlock* HeatLabel = New<UTextBlock>(Tree, TEXT("HeatLabel"));
		HeatLabel->SetText(FText::FromString(TEXT("HEAT")));
		HeatLabel->SetFont(MakeBMSPA(14, 4.f));
		HeatLabel->SetColorAndOpacity(FSlateColor(LabelHeat));
		if (UVerticalBoxSlot* S = HeatCluster->AddChildToVerticalBox(HeatLabel))
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UProgressBar* HeatBar = New<UProgressBar>(Tree, TEXT("HeatBar"));
		{
			FProgressBarStyle PS;
			PS.BackgroundImage = SolidBrush(BarTrack);
			PS.FillImage       = SolidBrush(HeatLow);
			HeatBar->SetWidgetStyle(PS);
		}
		HeatBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		HeatBar->SetPercent(0.6f);
		USizeBox* HeatSize = New<USizeBox>(Tree, TEXT("HeatSize"));
		HeatSize->SetWidthOverride(16.f);
		HeatSize->SetHeightOverride(112.f);
		HeatSize->AddChild(HeatBar);
		HeatCluster->AddChildToVerticalBox(HeatSize);

		// Thirst cluster
		UVerticalBox* ThirstCluster = New<UVerticalBox>(Tree, TEXT("ThirstCluster"));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(ThirstCluster))
		{
			S->SetVerticalAlignment(VAlign_Bottom);
		}
		UTextBlock* ThirstLabel = New<UTextBlock>(Tree, TEXT("ThirstLabel"));
		ThirstLabel->SetText(FText::FromString(TEXT("THIRST")));
		ThirstLabel->SetFont(MakeBMSPA(14, 4.f));
		ThirstLabel->SetColorAndOpacity(FSlateColor(LabelThirst));
		if (UVerticalBoxSlot* S = ThirstCluster->AddChildToVerticalBox(ThirstLabel))
			S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

		UProgressBar* ThirstBar = New<UProgressBar>(Tree, TEXT("ThirstBar"));
		{
			FProgressBarStyle PS;
			PS.BackgroundImage = SolidBrush(BarTrack);
			PS.FillImage       = SolidBrush(Cyan);
			ThirstBar->SetWidgetStyle(PS);
		}
		ThirstBar->SetBarFillType(EProgressBarFillType::BottomToTop);
		ThirstBar->SetPercent(0.8f);
		USizeBox* ThirstSize = New<USizeBox>(Tree, TEXT("ThirstSize"));
		ThirstSize->SetWidthOverride(16.f);
		ThirstSize->SetHeightOverride(112.f);
		ThirstSize->AddChild(ThirstBar);
		ThirstCluster->AddChildToVerticalBox(ThirstSize);
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
		Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.55f)));
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
		Panel->SetBrush(SolidBrush(FLinearColor(0.039f, 0.043f, 0.059f, 0.96f)));
		Panel->SetPadding(FMargin(0.f));
		Panel->SetHorizontalAlignment(HAlign_Fill);
		Panel->SetVerticalAlignment(VAlign_Center);
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
			S->SetZOrder(1);
		}

		UVerticalBox* Column = New<UVerticalBox>(Tree, TEXT("PauseColumn"));
		Panel->SetContent(Column);

		// Title — gigantic BMSPA cyan, sits above the buttons
		UTextBlock* Title = New<UTextBlock>(Tree, TEXT("PauseTitle"));
		Title->SetText(FText::FromString(TEXT("PAUSED")));
		Title->SetFont(MakeBMSPA(160, 14.f));
		Title->SetColorAndOpacity(FSlateColor(Cyan));
		Title->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 80.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}

		// Helper to build a button row. HAlign_Fill on the slot makes the
		// hit target span the whole panel width — full-bleed feel.
		auto MakeBtn = [&](const FString& Label, FName WidgetName)
		{
			UButton* Btn = New<UButton>(Tree, WidgetName);
			FButtonStyle BS;
			BS.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.05f));
			BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
			BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.22f));
			BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
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
				VS->SetPadding(FMargin(0.f, 14.f));   // pure vertical breathing — buttons span full width
				VS->SetHorizontalAlignment(HAlign_Fill);
			}
		};

		MakeBtn(TEXT("RESUME"),    TEXT("ResumeBtn"));
		MakeBtn(TEXT("SAVE"),      TEXT("SaveBtn"));
		MakeBtn(TEXT("LOAD"),      TEXT("LoadBtn"));
		MakeBtn(TEXT("MAIN MENU"), TEXT("MainMenuBtn"));
		MakeBtn(TEXT("QUIT"),      TEXT("QuitBtn"));

		// Status line — runtime sets save/load result here.
		UTextBlock* StatusText = New<UTextBlock>(Tree, TEXT("StatusText"));
		StatusText->SetText(FText::GetEmpty());
		StatusText->SetFont(MakeRodin(28));
		StatusText->SetColorAndOpacity(FSlateColor(CreamDim));
		StatusText->SetJustification(ETextJustify::Center);
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
