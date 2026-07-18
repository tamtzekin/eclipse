// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDialogueWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Dialogue panel — right-anchored, 480px wide, chalk-on-slate look.
//
//  Mirrors HTML #dialogue-box CSS (index.html L447):
//      width 480; full-height; right-anchored
//      background: linear-gradient(180deg, #0e0f12 → #08090c)
//      border-left: 1px solid rgba(241,236,217,0.85)
//      ::before — inset dashed inner border (chalk style)
//      font-family: RodinPro for body, BMSPA for speaker / close-X
//      color: #f1ecd9 (chalk cream)
//
//  Choices (HTML .dialogue-choice L741):
//      circle "node" 24×24 with BMSPA number, then text
//      cream on slate, hover white
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseDialogueWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("SpeakerNameText"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// ── Outer panel — slate gradient ────────────────────────────────────
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("DialoguePanel"));
		// CSS picks #08090c bottom which is darker; pick #0a0b0f as the avg.
		// Use RoundedBox (procedural) so we don't need a texture asset.
		Panel->SetBrush(SolidBrush(FLinearColor(0.039f, 0.043f, 0.059f, 0.97f)));
		Panel->SetPadding(FMargin(18.f, 14.f));

		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(Panel))
		{
			// 480 wide, full-height, anchored to right edge
			CSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
			CSlot->SetAlignment(FVector2D(1.f, 0.f));
			CSlot->SetPosition(FVector2D(0.f, 0.f));
			CSlot->SetSize(FVector2D(480.f, 0.f));
		}

		// ── Vertical column inside panel: speaker, body, choices ───────────
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("DialogueColumn"));
		Panel->SetContent(Column);

		// Speaker name (BMSPA, cyan)
		SpeakerNameText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("SpeakerNameText"));
		SpeakerNameText->SetFont(MakeBMSPA(/*Size=*/22, /*Letter=*/3.f));
		SpeakerNameText->SetColorAndOpacity(FSlateColor(Cyan));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(SpeakerNameText))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f));

		// Body text (RodinPro, cream)
		BodyText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("BodyText"));
		BodyText->SetFont(MakeRodin(/*Size=*/18));
		BodyText->SetColorAndOpacity(FSlateColor(Cream));
		BodyText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(BodyText))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 14.f));

		// Top divider line above choices (CSS dialogue-choices-inline)
		// Wrap a 1-px Border in a SizeBox so we get a real divider line.
		UBorder* Divider = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("ChoicesDivider"));
		Divider->SetBrush(SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.3f)));
		Divider->SetPadding(FMargin(0.f));
		USizeBox* DivSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("ChoicesDividerSize"));
		DivSize->SetHeightOverride(1.f);
		DivSize->AddChild(Divider);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(DivSize))
		{
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 8.f));
		}

		// Choices container — vertical list of buttons
		ChoicesBox = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("ChoicesBox"));
		Column->AddChildToVerticalBox(ChoicesBox);

		// ── Close button (chalk circle ×, top-right) ────────────────────────
		CloseButton = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), TEXT("CloseButton"));
		FButtonStyle CloseStyle;
		CloseStyle.Normal   = RoundedBrush(FLinearColor(0.031f, 0.035f, 0.047f, 0.6f),
		                                   FLinearColor(0.945f, 0.929f, 0.851f, 0.65f),
		                                   1.f, 14.f);   // 14 = half of 28 → circle
		CloseStyle.Hovered  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.08f),
		                                   FLinearColor::White, 1.f, 14.f);
		CloseStyle.Pressed  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f),
		                                   FLinearColor::White, 1.f, 14.f);
		CloseStyle.Disabled = CloseStyle.Normal;
		CloseButton->SetStyle(CloseStyle);

		UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("CloseLabel"));
		CloseLabel->SetText(FText::FromString(TEXT("×"))); // ×
		CloseLabel->SetFont(MakeBMSPA(16));
		CloseLabel->SetColorAndOpacity(FSlateColor(CreamDim));
		CloseLabel->SetJustification(ETextJustify::Center);
		CloseButton->SetContent(CloseLabel);

		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(CloseButton))
		{
			CSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
			CSlot->SetAlignment(FVector2D(1.f, 0.f));
			CSlot->SetPosition(FVector2D(-18.f, 14.f));
			CSlot->SetSize(FVector2D(28.f, 28.f));
			CSlot->SetZOrder(2);
		}
	}

	return Super::Initialize();
}

// ─────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────

void UEclipseDialogueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Note: DialogueMumbleSound is no longer auto-loaded here. It's now sourced
	// per-conversation from the speaking NPC's MumbleSound property in
	// HandleDialogueOpened — null for most NPCs, angel_voice for AngelSeeker.

	// ── Runtime injection of BodyWords if the WBP didn't ship one. ──
	// Has to run AFTER Super::Initialize() — i.e. here, not in our Initialize
	// override — because that's when the WBP's BindWidgetOptional bindings
	// (BodyText, BodyWords, etc.) are resolved. Doing this in Initialize()
	// before Super sees BodyText==null and the injection silently skips, which
	// is exactly why the body text was loading instantly: HandleNodeChanged
	// fell back to the legacy "BodyText->SetText" path because BodyWords was
	// never created.
	if (!BodyWords && BodyText && WidgetTree)
	{
		if (UPanelWidget* Parent = BodyText->GetParent())
		{
			BodyWords = WidgetTree->ConstructWidget<UWrapBox>(
				UWrapBox::StaticClass(), TEXT("BodyWords_Runtime"));
			BodyWords->SetInnerSlotPadding(FVector2D(0.f, 0.f));

			// Append the wrap box to the column, then shift it into the same
			// slot index BodyText occupied so the visual layout is preserved.
			const int32 BodyIdx = Parent->GetChildIndex(BodyText);
			Parent->AddChild(BodyWords);
			Parent->ShiftChild(FMath::Max(0, BodyIdx), BodyWords);

			if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(BodyWords->Slot))
			{
				S->SetPadding(FMargin(0.f, 4.f, 0.f, 14.f));
			}

			// Hide the static text block — the animated wrap box replaces it.
			BodyText->SetVisibility(ESlateVisibility::Collapsed);
			BodyText->SetText(FText::GetEmpty());

			UE_LOG(LogEclipse, Log, TEXT("Dlg: BodyWords injected at runtime (parent=%s, idx=%d)"),
				*Parent->GetName(), BodyIdx);
		}
		else
		{
			UE_LOG(LogEclipse, Warning, TEXT("Dlg: BodyText has no parent — cannot inject BodyWords."));
		}
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("Dlg: BodyWords inject skipped — BodyWords=%s BodyText=%s"),
			BodyWords ? TEXT("already-bound") : TEXT("null"),
			BodyText  ? TEXT("BOUND")          : TEXT("null"));
	}

	// ── Unified dialogue panel (Disco-Elysium-style box) ──────────────────
	//
	// One outer UBorder containing the history scroll up top and the
	// choices at the bottom, with a thin divider between. Earlier iteration
	// used two L/R history scrolls on the screen edges, but a single panel
	// reads cleaner with stacked bubbles (NPC right-aligned, player
	// left-aligned within the same column).
	//
	// LeftHistoryScroll is left null — kept on the class for ABI stability;
	// MakeChoice now appends player bubbles to RightHistoryScroll with
	// HAlign_Left, distinguishing speakers via in-column alignment + caption
	// colour. The whole layout is right-anchored 500px wide, ~90% screen
	// height, with a soft dark fill + cream chalk outline.
	if (WidgetTree && !RightHistoryScroll)
	{
		using namespace EclipseUI;

		UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
		UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(RootPanel);
		if (RootCanvas)
		{
			UBorder* OuterBox = WidgetTree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), TEXT("DialogueOuterBox"));
			OuterBox->SetBrush(RoundedBrush(
				FLinearColor(0.039f, 0.043f, 0.059f, 0.92f),
				FLinearColor(0.945f, 0.929f, 0.851f, 0.40f),
				1.5f, 8.f));
			OuterBox->SetPadding(FMargin(16.f, 14.f));
			if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(OuterBox))
			{
				CS->SetAnchors(FAnchors(1.f, 0.05f, 1.f, 0.95f));
				CS->SetAlignment(FVector2D(1.f, 0.f));
				CS->SetPosition(FVector2D(-20.f, 0.f));
				CS->SetSize(FVector2D(500.f, 0.f));
				CS->SetZOrder(1);
			}

			UVerticalBox* OuterCol = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("DialogueOuterColumn"));
			OuterBox->SetContent(OuterCol);

			RightHistoryScroll = WidgetTree->ConstructWidget<UScrollBox>(
				UScrollBox::StaticClass(), TEXT("RightHistoryScroll_Runtime"));
			RightHistoryScroll->SetAnimateWheelScrolling(true);
			RightHistoryScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			if (UVerticalBoxSlot* VS = OuterCol->AddChildToVerticalBox(RightHistoryScroll))
			{
				// Fill the available vertical space so the scroll grows with
				// the panel; choices below get whatever height they need.
				VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			// Divider between bubble transcript and choices.
			{
				UBorder* Div = WidgetTree->ConstructWidget<UBorder>(
					UBorder::StaticClass(), TEXT("OuterDividerLine"));
				Div->SetBrush(SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.28f)));
				Div->SetPadding(FMargin(0.f));
				USizeBox* DivSize = WidgetTree->ConstructWidget<USizeBox>(
					USizeBox::StaticClass(), TEXT("OuterDividerSize"));
				DivSize->SetHeightOverride(1.f);
				DivSize->AddChild(Div);
				if (UVerticalBoxSlot* VS = OuterCol->AddChildToVerticalBox(DivSize))
				{
					VS->SetPadding(FMargin(0.f, 8.f, 0.f, 8.f));
				}
			}

			UE_LOG(LogEclipse, Log,
				TEXT("Dlg: DialogueOuterBox injected (500w, 90%% height, right)"));
		}
	}

	// ── Force the canonical column order. ──
	// Belt-and-braces: regardless of how the WBP was authored, what
	// PopulateDialogueWBP wrote, or any prior runtime injection, walk the
	// vertical box parent of BodyWords and slot every known child into the
	// expected order. This is what guarantees the choices stay UNDER the body
	// — without it the layout depends on whatever ShiftChild left behind,
	// which can drift if the asset was previously saved with a different
	// layout.
	{
		UVerticalBox* Col = nullptr;
		if (BodyWords)               Col = Cast<UVerticalBox>(BodyWords->GetParent());
		if (!Col && BodyText)        Col = Cast<UVerticalBox>(BodyText->GetParent());
		if (!Col && ChoicesBox)      Col = Cast<UVerticalBox>(ChoicesBox->GetParent());

		if (Col)
		{
			auto MoveTo = [Col](UWidget* W, int32 Idx)
			{
				if (W && Col->GetChildIndex(W) != INDEX_NONE)
				{
					Col->ShiftChild(Idx, W);
				}
			};

			int32 Idx = 0;
			MoveTo(SpeakerNameText, Idx++);
			MoveTo(BodyWords,       Idx++);
			MoveTo(BodyText,        Idx++);

			// Divider can be either ChoicesDividerSize (SizeBox wrapper) or
			// the raw ChoicesDivider Border, depending on which populator
			// version baked the asset.
			UWidget* Div = WidgetTree ? WidgetTree->FindWidget(TEXT("ChoicesDividerSize")) : nullptr;
			if (!Div && WidgetTree) Div = WidgetTree->FindWidget(TEXT("ChoicesDivider"));
			MoveTo(Div, Idx++);

			MoveTo(ChoicesBox, Idx++);

			UE_LOG(LogEclipse, Log, TEXT("Dlg: Column re-ordered (%d children)"), Col->GetChildrenCount());
		}
	}

	// ── Speech-bubble layout restyle ──────────────────────────────────────
	//
	// The widget used to render the current line in a solid right-anchored
	// panel. With the L/R history scroll boxes injected above, each line is
	// instead a discrete bubble appended to one of those scrolls. The big
	// background panel + its in-column speaker / body / effects widgets are
	// now dead weight visually — collapse them so only the choice buttons
	// keep their existing position. `BodyWords` is NOT collapsed because
	// HandleNodeChanged repoints the pointer at each new bubble's WrapBox
	// before driving StartBodyAnimation — the in-column instance is left
	// behind (invisible) once the first node arrives.
	#if 1
	{
		using namespace EclipseUI;

		// 1. Outer panel → fully transparent.
		//    The populator builds DialoguePanel as a UOverlay containing five
		//    stacked PanelFade_0..4 UBorders that fake a radial edge-fade
		//    (no native gradient brush in Slate). Collapse all of them so the
		//    "solid right-side rectangle" disappears.
		//
		//    The C++ fallback layout uses a single UBorder also called
		//    DialoguePanel — handle that case too.
		if (WidgetTree)
		{
			if (UBorder* DialoguePanelAsBorder = Cast<UBorder>(
					WidgetTree->FindWidget(TEXT("DialoguePanel"))))
			{
				DialoguePanelAsBorder->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.f)));
				DialoguePanelAsBorder->SetPadding(FMargin(18.f, 14.f));
				UE_LOG(LogEclipse, Log, TEXT("Dlg: DialoguePanel(Border) → transparent"));
			}
			// Hide each PanelFade_N layer (UOverlay-based populator path).
			int32 HiddenLayers = 0;
			for (int32 i = 0; i < 8; ++i)
			{
				const FName LayerName(*FString::Printf(TEXT("PanelFade_%d"), i));
				if (UWidget* Layer = WidgetTree->FindWidget(LayerName))
				{
					Layer->SetVisibility(ESlateVisibility::Collapsed);
					++HiddenLayers;
				}
			}
			if (HiddenLayers > 0)
			{
				UE_LOG(LogEclipse, Log, TEXT("Dlg: collapsed %d PanelFade_N layer(s)"), HiddenLayers);
			}
		}

		// 2. Collapse the in-column speaker / body / effects widgets.
		//    Each new dialogue line is now a self-contained bubble appended
		//    to LeftHistoryScroll / RightHistoryScroll — the original
		//    column-stacked instances render nothing and just take up space.
		//    BodyWords is REPOINTED in HandleNodeChanged (not collapsed
		//    here) because StartBodyAnimation drives it; collapsing the
		//    in-column WrapBox is fine because we redirect the pointer
		//    before the first node arrives.
		if (SpeakerNameText) SpeakerNameText->SetVisibility(ESlateVisibility::Collapsed);
		if (BodyText)        BodyText->SetVisibility(ESlateVisibility::Collapsed);
		if (EffectsLineText) EffectsLineText->SetVisibility(ESlateVisibility::Collapsed);
		if (BodyWords)       BodyWords->SetVisibility(ESlateVisibility::Collapsed);

		// 3. Re-style each pre-built choice button as its own bubble. Drops
		//    the prior cream-tint button look in favour of a black cloud
		//    that matches the speech-bubble look of the L/R history scrolls.
		auto BubbleBrush = [](float Alpha)
		{
			return RoundedBrush(
				FLinearColor(0.f, 0.f, 0.f, Alpha),
				FLinearColor(0.945f, 0.929f, 0.851f, 0.18f),
				1.f, 10.f);
		};
		UButton* ChoiceBtns[] = { ChoiceBtn_0, ChoiceBtn_1, ChoiceBtn_2, ChoiceBtn_3, ChoiceBtn_4 };
		for (UButton* Btn : ChoiceBtns)
		{
			if (!Btn) continue;
			FButtonStyle BS;
			BS.Normal   = BubbleBrush(0.55f);
			BS.Hovered  = BubbleBrush(0.72f);
			BS.Pressed  = BubbleBrush(0.85f);
			BS.Disabled = BubbleBrush(0.35f);
			Btn->SetStyle(BS);
		}

		// 4. Hide the choices-divider — bubbles imply their own grouping.
		if (UWidget* Div = WidgetTree ? WidgetTree->FindWidget(TEXT("ChoicesDividerSize")) : nullptr)
		{
			Div->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (UWidget* Div = WidgetTree ? WidgetTree->FindWidget(TEXT("ChoicesDivider")) : nullptr)
		{
			Div->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 5. Reparent ChoicesBox into the DialogueOuterColumn (below the
		//    bubble scroll + divider). Both populator paths (C++ fallback +
		//    EclipseUiBuilder) park ChoiceBtn_N inside ChoicesBox, so
		//    reparenting ChoicesBox carries all buttons with it. The outer
		//    column already has the history scroll filling its top portion
		//    and a 1-px divider below it — ChoicesBox becomes the third
		//    (auto-sized) child below the divider, giving the DE-style
		//    "transcript on top, choices below" layout inside a single
		//    box.
		if (ChoicesBox && WidgetTree)
		{
			UVerticalBox* OuterCol = Cast<UVerticalBox>(
				WidgetTree->FindWidget(TEXT("DialogueOuterColumn")));
			const bool bAlreadyInOuter = OuterCol &&
				(ChoicesBox->GetParent() == OuterCol);
			if (OuterCol && !bAlreadyInOuter)
			{
				ChoicesBox->RemoveFromParent();
				OuterCol->AddChildToVerticalBox(ChoicesBox);
				UE_LOG(LogEclipse, Log,
					TEXT("Dlg: ChoicesBox reparented into DialogueOuterColumn (below divider)"));
			}
		}
	}
	#endif // bubble runtime override

	// Hidden until a dialogue opens
	SetVisibility(ESlateVisibility::Collapsed);

	if (CloseButton)
	{
		CloseButton->SetClickMethod(EButtonClickMethod::MouseDown);
		CloseButton->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnCloseClicked);
	}

	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
	{
		DS->OnDialogueOpened.AddDynamic(this, &UEclipseDialogueWidget::HandleDialogueOpened);
		DS->OnNodeChanged.AddDynamic(this, &UEclipseDialogueWidget::HandleNodeChanged);
		DS->OnDialogueClosed.AddDynamic(this, &UEclipseDialogueWidget::HandleDialogueClosed);
	}
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
	{
		GS->OnStatXPGranted.AddDynamic(this, &UEclipseDialogueWidget::HandleStatXPGranted);
	}
}

void UEclipseDialogueWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			DS->OnDialogueOpened.RemoveDynamic(this, &UEclipseDialogueWidget::HandleDialogueOpened);
			DS->OnNodeChanged.RemoveDynamic(this, &UEclipseDialogueWidget::HandleNodeChanged);
			DS->OnDialogueClosed.RemoveDynamic(this, &UEclipseDialogueWidget::HandleDialogueClosed);
		}
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnStatXPGranted.RemoveDynamic(this, &UEclipseDialogueWidget::HandleStatXPGranted);
		}
	}
	Super::NativeDestruct();
}

// ─────────────────────────────────────────────────────────────
//  Speech-bubble construction
//
//  AppendBubble builds one black semi-transparent "cloud" (a UBorder with
//  a UVerticalBox of speaker caption + UWrapBox of words + optional effects
//  line) and appends it to the supplied UScrollBox. The wrap box is
//  returned so the caller can hook the existing word-by-word fade-in
//  (HandleNodeChanged repoints `BodyWords` here so StartBodyAnimation
//  lands on the new bubble's container).
//
//  bAlignRight controls which screen edge the bubble hugs inside its
//  scroll box — NPC lines go right, the player's chosen lines go left.
//  WrapSize is set explicitly on the inner WrapBox so long lines wrap
//  within the bubble instead of pushing it off the visible scroll area.
// ─────────────────────────────────────────────────────────────

UWrapBox* UEclipseDialogueWidget::AppendBubble(UScrollBox* Box,
	const FText& SpeakerCaption, const FLinearColor& CaptionTint,
	bool bAlignRight, UTextBlock** EffectsOut)
{
	using namespace EclipseUI;
	if (!Box || !WidgetTree) return nullptr;

	UBorder* Bubble = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), NAME_None);
	Bubble->SetBrush(RoundedBrush(
		FLinearColor(0.f, 0.f, 0.f, 0.70f),                       // black 70%
		FLinearColor(0.945f, 0.929f, 0.851f, 0.18f),              // soft chalk outline
		1.f, 12.f));
	Bubble->SetPadding(FMargin(14.f, 10.f));
	Bubble->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), NAME_None);
	Bubble->SetContent(Col);

	// Speaker caption — small BMSPA tint matching the side (cyan for NPC,
	// cream for player by convention; caller passes the colour).
	UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), NAME_None);
	Caption->SetFont(MakeBMSPA(/*Size=*/14, /*Letter=*/2.f));
	Caption->SetColorAndOpacity(FSlateColor(CaptionTint));
	Caption->SetText(SpeakerCaption);
	Caption->SetJustification(bAlignRight ? ETextJustify::Right : ETextJustify::Left);
	if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Caption))
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

	// Inner WrapBox for per-word fade-in. Explicit wrap so long lines
	// stack rows instead of stretching the bubble off the scroll edge.
	UWrapBox* Words = WidgetTree->ConstructWidget<UWrapBox>(
		UWrapBox::StaticClass(), NAME_None);
	Words->SetInnerSlotPadding(FVector2D(0.f, 0.f));
	Words->SetExplicitWrapSize(true);
	Words->SetWrapSize(320.f);
	Col->AddChildToVerticalBox(Words);

	// Optional effects line under the body words. Collapsed by default —
	// the caller sets text + flips visibility if the node has any stage
	// directives.
	if (EffectsOut)
	{
		UTextBlock* EffectsBlk = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		EffectsBlk->SetFont(MakeRodin(14));
		EffectsBlk->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.65f, 0.25f, 0.95f)));
		EffectsBlk->SetAutoWrapText(true);
		EffectsBlk->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(EffectsBlk))
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		*EffectsOut = EffectsBlk;
	}

	// Slot the bubble into the scroll box with edge-aligned HAlign so the
	// bubble sizes to content and hugs the appropriate side.
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(Box->AddChild(Bubble)))
	{
		SS->SetHorizontalAlignment(bAlignRight ? HAlign_Right : HAlign_Left);
		SS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	// Auto-scroll so the newest bubble is always visible.
	Box->ScrollToEnd();

	return Words;
}

// ─────────────────────────────────────────────────────────────
//  Delegate handlers
// ─────────────────────────────────────────────────────────────

void UEclipseDialogueWidget::HandleDialogueOpened(AEclipseNpcCharacter* Npc)
{
	SetVisibility(ESlateVisibility::Visible);

	// Audio cue on dialogue open. Null-safe — no-op without an assigned sound.
	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueOpenSound);
	}

	// ── Per-NPC mumble voice ──
	// The mumble track is sourced from the speaking NPC's MumbleSound — null
	// for most NPCs, which gives them a silent dialogue cascade. Only the
	// Angel (matched by NpcName, NOT bIsAngelSeeker — that flag is set on the
	// bathroom girl who SEEKS the angel) gets a hard-coded fallback to the
	// canonical /Game/Audio/angel_voice clip if her BP forgot to wire it up.
	const bool bSpeakerIsAngel = Npc && (Npc->NpcName == FName(TEXT("Angel")));

	DialogueMumbleSound = Npc ? Npc->MumbleSound.Get() : nullptr;
	if (!DialogueMumbleSound && bSpeakerIsAngel)
	{
		DialogueMumbleSound = LoadObject<USoundBase>(nullptr,
			TEXT("/Game/Audio/angel_voice.angel_voice"));
	}
	// If we resolved a sound but the speaker is NOT angel and the designer
	// hasn't given them their own mumble, drop it — defensive safety in case
	// stale state slips through.
	if (DialogueMumbleSound && !bSpeakerIsAngel && (!Npc || !Npc->MumbleSound))
	{
		DialogueMumbleSound = nullptr;
	}

	// Reset the playback cursor so each new conversation starts at the top
	// of the source clip — the melody plays in order from word one.
	MumbleCursor = 0.f;
	UE_LOG(LogEclipse, Log, TEXT("Dlg: opened with NPC '%s' (NpcName='%s') angel=%s mumble=%s"),
		Npc ? *Npc->GetName() : TEXT("<null>"),
		Npc ? *Npc->NpcName.ToString() : TEXT("<null>"),
		bSpeakerIsAngel ? TEXT("YES") : TEXT("no"),
		DialogueMumbleSound ? TEXT("set") : TEXT("none"));

	// Swap in the speaker portrait if one is assigned on the NPC.
	if (SpeakerPortrait)
	{
		if (Npc && Npc->PortraitTexture)
		{
			SpeakerPortrait->SetBrushFromTexture(Npc->PortraitTexture, /*bMatchSize=*/false);
			SpeakerPortrait->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else
		{
			SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// Take keyboard focus so W/S/E navigation works straight away.
	SetIsFocusable(true);
	SetKeyboardFocus();

	// Disable player movement while talking — matches JS prototype which freezes
	// the controller during dialogue.
	//
	// SetWidgetToFocus(TakeWidget()) is needed so the FIRST click on a choice
	// button registers as a click — without an initial focus target, Slate
	// uses the first click to *give* focus and the second to actually fire
	// OnClicked. TakeWidget() on the outer dialogue widget is stable because
	// only ChoicesBox children get rebuilt, not the parent SObjectWidget.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
}

void UEclipseDialogueWidget::HandleNodeChanged(FEclipseDialogueNodeView Node)
{
	using namespace EclipseUI;

	// SelfHitTestInvisible: the root canvas itself ignores hit-testing so only
	// the actual buttons/panel intercept input. Without this, the fullscreen
	// canvas would be a hit-test layer over the whole viewport, producing
	// off-centre hover/click registration on the buttons inside.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Keep the legacy in-column speaker label up-to-date (it's collapsed in
	// NativeConstruct so nothing renders, but the assignment is harmless and
	// keeps a fallback if the bubble path fails to spawn for any reason).
	if (SpeakerNameText)
		SpeakerNameText->SetText(FText::FromName(Node.SpeakerName));

	// Append a new NPC-side bubble and redirect the per-word animation +
	// effects-line pointers so the existing machinery lands in the new
	// bubble. The old WrapBox stays where it is in the prior bubble (in the
	// scroll box) with its words intact — the history accumulates.
	if (RightHistoryScroll)
	{
		UTextBlock* BubbleEffects = nullptr;
		UWrapBox* BubbleWords = AppendBubble(
			RightHistoryScroll,
			FText::FromName(Node.SpeakerName),
			Cyan,                          // NPC caption tint
			/*bAlignRight=*/true,
			&BubbleEffects);

		if (BubbleWords)
		{
			BodyWords        = BubbleWords;
			EffectsLineText  = BubbleEffects;
			StartBodyAnimation(Node.Body.ToString());
		}
	}
	else if (BodyWords)
	{
		// Legacy in-column path — only reached if history injection failed.
		if (BodyText)
		{
			BodyText->SetText(FText::GetEmpty());
			BodyText->SetVisibility(ESlateVisibility::Collapsed);
		}
		StartBodyAnimation(Node.Body.ToString());
	}
	else if (BodyText)
	{
		BodyText->SetText(Node.Body);
	}

	// Runtime-inject + render the orange effects line below the body if the
	// fragment has any. Empty Text → keep widget collapsed so it doesn't
	// occupy space.
	if (!EffectsLineText && WidgetTree)
	{
		// Find the column that holds the body + choices (the dialogue
		// populators name it "DialogueColumn"; fall back to BodyText's parent).
		UVerticalBox* Col = Cast<UVerticalBox>(WidgetTree->FindWidget(TEXT("DialogueColumn")));
		if (!Col && BodyText)        Col = Cast<UVerticalBox>(BodyText->GetParent());
		if (!Col && BodyWords)
		{
			UPanelWidget* P = BodyWords->GetParent();
			while (P && !P->IsA<UVerticalBox>()) P = P->GetParent();
			Col = Cast<UVerticalBox>(P);
		}

		if (Col)
		{
			EffectsLineText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("EffectsLineText_Runtime"));
			EffectsLineText->SetFont(EclipseUI::MakeRodin(14));
			// Orange tint, slightly muted so it doesn't fight the cream body.
			EffectsLineText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.65f, 0.25f, 0.95f)));
			EffectsLineText->SetAutoWrapText(true);
			Col->AddChild(EffectsLineText);
			// Slot it directly after the body wrap-box / static body so it
			// reads as a follow-on line. Default position is end-of-column.
			UWidget* BodyAnchor = BodyWords ? (UWidget*)BodyWords : (UWidget*)BodyText;
			if (BodyAnchor)
			{
				const int32 BodyIdx = Col->GetChildIndex(BodyAnchor);
				if (BodyIdx >= 0)
				{
					Col->ShiftChild(BodyIdx + 1, EffectsLineText);
				}
			}
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(EffectsLineText->Slot))
			{
				VS->SetPadding(FMargin(0.f, 6.f, 0.f, 6.f));
			}
			UE_LOG(LogEclipse, Log, TEXT("Dlg: EffectsLineText injected at runtime"));
		}
	}
	if (EffectsLineText)
	{
		if (Node.EffectsLine.IsEmpty())
		{
			EffectsLineText->SetVisibility(ESlateVisibility::Collapsed);
		}
		else
		{
			EffectsLineText->SetText(Node.EffectsLine);
			EffectsLineText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
	}

	RebuildChoices(Node.Choices);
}

void UEclipseDialogueWidget::HandleStatXPGranted(FName StatKey, int32 Amount, int32 NewLevel, bool bLeveledUp)
{
	// Only annotate an open transcript — XP granted outside dialogue (future
	// systems) has nowhere sensible to land here.
	if (!RightHistoryScroll || !WidgetTree || GetVisibility() == ESlateVisibility::Collapsed) return;

	// Mint green — deliberately outside the transcript's palette (cream
	// player / cyan NPC / orange effects) so XP reads as a system event.
	const FLinearColor XPGreen(0.35f, 1.f, 0.60f, 0.95f);

	FString Msg = FString::Printf(TEXT("%s: +%d XP"), *StatKey.ToString().ToUpper(), Amount);
	if (bLeveledUp)
	{
		Msg += FString::Printf(TEXT("  —  LEVEL UP! %d"), NewLevel);
	}

	UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), NAME_None);
	Line->SetFont(EclipseUI::MakeBMSPA(/*Size=*/13, /*Letter=*/1.5f));
	Line->SetColorAndOpacity(FSlateColor(XPGreen));
	Line->SetText(FText::FromString(Msg));

	// Naked line (no bubble), left-aligned under the player's chosen line.
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(RightHistoryScroll->AddChild(Line)))
	{
		SS->SetHorizontalAlignment(HAlign_Left);
		SS->SetPadding(FMargin(6.f, 0.f, 0.f, 10.f));
	}
	RightHistoryScroll->ScrollToEnd();
}

void UEclipseDialogueWidget::HandleDialogueClosed()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (SpeakerNameText) SpeakerNameText->SetText(FText::GetEmpty());
	if (BodyText)        BodyText->SetText(FText::GetEmpty());
	if (SpeakerPortrait) SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);

	// Clear the bubble transcript so the next conversation starts fresh —
	// otherwise the previous NPC's lines would still be stacked when the
	// player walks up to a different character. Same for the player-side
	// scroll.
	if (LeftHistoryScroll)  LeftHistoryScroll->ClearChildren();
	if (RightHistoryScroll) RightHistoryScroll->ClearChildren();
	CurrentChoices.Reset();

	// Tear down the per-word animation state so we don't keep ticking dead
	// references on the next NativeTick.
	bDialogueAnimating = false;
	DialogueAnimTime = 0.f;
	if (BodyWords) BodyWords->ClearChildren();
	AnimWordBlocks.Reset();
	AnimWordDelays.Reset();
	AnimWordTints.Reset();
	AnimWordMumbleFired.Reset();
	ChoiceRevealButtons.Reset();
	ChoiceRevealDelays.Reset();

	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueCloseSound);
	}

	// Drop the mumble reference so the next conversation re-evaluates which
	// NPC is speaking — otherwise a previously-angel widget would keep playing
	// angel_voice for any subsequent NPC.
	DialogueMumbleSound = nullptr;
	MumbleCursor = 0.f;

	// Cut any still-ringing mumble slices when the panel closes.
	for (TWeakObjectPtr<UAudioComponent>& Weak : ActiveMumbleSlices)
	{
		if (UAudioComponent* Live = Weak.Get())
		{
			Live->FadeOut(0.06f, 0.f);
		}
	}
	ActiveMumbleSlices.Reset();

	// Path A — designer pre-built rows: just collapse, never remove from tree.
	// (Calling ClearChildren() would orphan the bound buttons so subsequent
	// SetVisibility() calls render nothing, even though the C++ refs remain
	// valid — that's the "interactable but invisible on second dialogue" bug.)
	UButton* PreBtns[] = { ChoiceBtn_0, ChoiceBtn_1, ChoiceBtn_2, ChoiceBtn_3, ChoiceBtn_4 };
	if (PreBtns[0] != nullptr)
	{
		for (UButton* Btn : PreBtns)
		{
			if (Btn) Btn->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	else if (ChoicesBox)
	{
		// Path B — dynamic construction: safe to clear because we'll rebuild.
		ChoicesBox->ClearChildren();
	}
	ChoiceButtons.Reset();
	SelectedIndex = 0;

	// Restore default gameplay input: cursor hidden, mouse drives the camera.
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
}

// ─────────────────────────────────────────────────────────────
//  Choice buttons — circle-numbered + text, mirrors HTML .dialogue-choice
// ─────────────────────────────────────────────────────────────

void UEclipseDialogueWidget::RebuildChoices(const TArray<FEclipseDialogueChoice>& Choices)
{
	using namespace EclipseUI;

	// Cache the choices so MakeChoice can pull the chosen line's text and
	// stamp a player-side bubble into LeftHistoryScroll before the dialogue
	// subsystem advances to the next node (after which Choices has changed).
	CurrentChoices = Choices;

	constexpr int32 MaxSlots = 5;

	// ── Path A: WBP has pre-built ChoiceBtn_0..4 (designer-styled) ──
	UButton*    PreBtns[]  = { ChoiceBtn_0,  ChoiceBtn_1,  ChoiceBtn_2,  ChoiceBtn_3,  ChoiceBtn_4  };
	UTextBlock* PreTexts[] = { ChoiceText_0, ChoiceText_1, ChoiceText_2, ChoiceText_3, ChoiceText_4 };

	if (PreBtns[0] != nullptr)
	{
		ChoiceButtons.Reset();
		ChoiceRevealButtons.Reset();
		ChoiceRevealDelays.Reset();
		for (int32 i = 0; i < MaxSlots; ++i)
		{
			UButton*    Btn   = PreBtns[i];
			UTextBlock* Label = PreTexts[i];
			if (!Btn) continue;

			// Fire on press, not press+release — avoids the "feels like double-
			// click" symptom from UE's default DownAndUp ClickMethod when
			// FInputModeUIOnly first-click consumes focus and the second click
			// finally registers as the press.
			Btn->SetClickMethod(EButtonClickMethod::MouseDown);

			// Always (re)hook the callback. AddDynamic stringifies the function
			// name at the macro callsite, so we need an explicit literal per slot
			// — using a function-pointer array would bind to the FName "Callbacks[i]"
			// and silently fail at runtime ("Unable to bind delegate to 'None'").
			Btn->OnClicked.Clear();
			switch (i)
			{
				case 0: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice0); break;
				case 1: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice1); break;
				case 2: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice2); break;
				case 3: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice3); break;
				case 4: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice4); break;
			}

			if (i < Choices.Num())
			{
				const FEclipseDialogueChoice& Choice = Choices[i];
				// Failed skill-check choices stay CLICKABLE so the player can
				// attempt them at an Energy cost — see DialogueSubsystem::MakeChoice.
				// Stage-directive gates (StatGate / ItemGate) genuinely block
				// the action, so those buttons are disabled.
				const bool bHasGateHint = !Choice.GateHint.IsEmpty();
				Btn->SetIsEnabled(!bHasGateHint);

				FString S = Choice.Text.ToString();
				if (Choice.bIsSkillCheck && !Choice.bAvailable)
				{
					S += FString::Printf(TEXT("  [-%d STIMULATION]"), Choice.StimulationDamageOnFail);
				}
				if (bHasGateHint)
				{
					S += TEXT("  ") + Choice.GateHint.ToString();
				}
				const FLinearColor Tint = Choice.bAvailable
					? Cream
					: FLinearColor(0.85f, 0.45f, 0.40f, 1.f);   // red-tinted hint for risky picks

				// Each choice row stays Collapsed until the body has finished
				// cascading; then they ripple in one-by-one. NativeTick flips
				// these to Visible once DialogueAnimTime crosses StartDelay.
				const float ChoiceStagger = 0.12f;   // s between rows
				const float StartDelay    = BodyAnimTotalTime + ChoiceStagger * static_cast<float>(i);

				if (BodyWords)
				{
					Btn->SetVisibility(ESlateVisibility::Collapsed);
					AnimateChoiceText(Label, i, S, Tint, StartDelay);

					ChoiceRevealButtons.Add(Btn);
					ChoiceRevealDelays.Add(StartDelay);
				}
				else if (Label)
				{
					// No body animation infra at all — just set the static label.
					Btn->SetVisibility(ESlateVisibility::Visible);
					Label->SetText(FText::FromString(S));
					Label->SetColorAndOpacity(FSlateColor(Tint));
				}
				ChoiceButtons.Add(Btn);
			}
			else
			{
				Btn->SetVisibility(ESlateVisibility::Collapsed);
				// Also collapse any leftover word-wrap from a previous node
				// with more choices, so empty rows don't take vertical space.
				const FName WBName(*FString::Printf(TEXT("ChoiceWords_%d"), i));
				if (UWidget* W = WidgetTree ? WidgetTree->FindWidget(WBName) : nullptr)
				{
					W->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
		// Default selection on first available
		SelectedIndex = 0;
		for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
			if (ChoiceButtons[i]->GetIsEnabled()) { SelectedIndex = i; break; }
		HighlightChoice(SelectedIndex);
		return;
	}

	// ── Path B: dynamic construction (fallback if WBP didn't provide rows) ──
	if (!ChoicesBox) return;
	ChoicesBox->ClearChildren();
	ChoiceButtons.Reset();

	for (int32 i = 0; i < Choices.Num() && i < MaxSlots; ++i)
	{
		const FEclipseDialogueChoice& Choice = Choices[i];

		// Each row is: [num-circle] [text]
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceBtn_%d"), i)));

		FButtonStyle BtnStyle;
		// Subtle visible Normal state so the rows are clearly clickable
		// (was fully transparent before — caused buttons to "disappear").
		BtnStyle.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.04f));
		BtnStyle.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.10f));
		BtnStyle.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.16f));
		BtnStyle.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.02f));
		Btn->SetStyle(BtnStyle);
		// Failed skill-check choices stay CLICKABLE — see MakeChoice for the
		// Energy-cost handling. But stage-directive gates ([STAT: N] /
		// [ITEM_NAME]) genuinely block: disable when GateHint is set.
		Btn->SetIsEnabled(Choice.GateHint.IsEmpty());

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceRow_%d"), i)));

		// ── Circle number node (24x24) ──
		UBorder* CircleBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceCircle_%d"), i)));
		CircleBg->SetBrush(RoundedBrush(
			FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),  // bg
			FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),  // chalk outline
			1.f, 12.f));
		CircleBg->SetPadding(FMargin(0.f));
		CircleBg->SetHorizontalAlignment(HAlign_Center);
		CircleBg->SetVerticalAlignment(VAlign_Center);

		UTextBlock* CircleNum = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceNum_%d"), i)));
		CircleNum->SetText(FText::AsNumber(i + 1));
		CircleNum->SetFont(MakeBMSPA(11));
		CircleNum->SetColorAndOpacity(FSlateColor(Cream));
		CircleNum->SetJustification(ETextJustify::Center);
		CircleBg->SetContent(CircleNum);

		// Wrap the circle in a SizeBox to enforce 24×24 footprint.
		USizeBox* CircleSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceCircleSize_%d"), i)));
		CircleSize->SetWidthOverride(24.f);
		CircleSize->SetHeightOverride(24.f);
		CircleSize->AddChild(CircleBg);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(CircleSize))
		{
			S->SetPadding(FMargin(0.f, 1.f, 10.f, 0.f));
			S->SetVerticalAlignment(VAlign_Top);
		}

		// ── Text label ──
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceText_%d"), i)));
		FString LabelStr = Choice.Text.ToString();
		if (Choice.bIsSkillCheck && !Choice.bAvailable)
		{
			LabelStr += FString::Printf(TEXT("  [-%d STIMULATION]"), Choice.StimulationDamageOnFail);
		}
		if (!Choice.GateHint.IsEmpty())
		{
			LabelStr += TEXT("  ") + Choice.GateHint.ToString();
		}
		Label->SetText(FText::FromString(LabelStr));
		Label->SetFont(MakeRodin(15));
		Label->SetColorAndOpacity(FSlateColor(Choice.bAvailable
			? Cream
			: FLinearColor(0.85f, 0.45f, 0.40f, 1.f)));
		Label->SetAutoWrapText(true);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Label))
		{
			S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			S->SetVerticalAlignment(VAlign_Center);
		}

		Btn->SetContent(Row);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);
		switch (i)
		{
			case 0: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice0); break;
			case 1: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice1); break;
			case 2: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice2); break;
			case 3: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice3); break;
			case 4: Btn->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnChoice4); break;
		}

		if (UVerticalBoxSlot* S = ChoicesBox->AddChildToVerticalBox(Btn))
		{
			S->SetPadding(FMargin(0.f, 4.f));
		}

		ChoiceButtons.Add(Btn);
	}

	// Default selection to the first available choice
	SelectedIndex = 0;
	for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
	{
		if (ChoiceButtons[i]->GetIsEnabled()) { SelectedIndex = i; break; }
	}
	HighlightChoice(SelectedIndex);
}

void UEclipseDialogueWidget::HighlightChoice(int32 Index)
{
	using namespace EclipseUI;
	for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
	{
		UButton* Btn = ChoiceButtons[i];
		if (!Btn) continue;
		const bool bSel = (i == Index);
		FButtonStyle S = Btn->GetStyle();
		S.Normal = SolidBrush(bSel
			? FLinearColor(0.945f, 0.929f, 0.851f, 0.18f)   // selected — bright cream
			: FLinearColor(0.945f, 0.929f, 0.851f, 0.04f)); // unselected
		Btn->SetStyle(S);
	}
}

void UEclipseDialogueWidget::NavigateChoice(int32 Delta)
{
	if (ChoiceButtons.Num() == 0) return;
	int32 N = ChoiceButtons.Num();
	int32 Try = SelectedIndex;
	for (int32 i = 0; i < N; ++i)
	{
		Try = (Try + Delta + N) % N;
		if (ChoiceButtons[Try] && ChoiceButtons[Try]->GetIsEnabled())
		{
			SelectedIndex = Try;
			HighlightChoice(SelectedIndex);
			return;
		}
	}
}

FReply UEclipseDialogueWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	if (K == EKeys::W || K == EKeys::Up)         { NavigateChoice(-1); return FReply::Handled(); }
	if (K == EKeys::S || K == EKeys::Down)       { NavigateChoice(+1); return FReply::Handled(); }
	if (K == EKeys::E || K == EKeys::Enter ||
	    K == EKeys::SpaceBar)                    { MakeChoice(SelectedIndex); return FReply::Handled(); }
	if (K == EKeys::Escape)                      { OnCloseClicked(); return FReply::Handled(); }
	// Number keys 1-5 jump-select
	for (int32 i = 0; i < ChoiceButtons.Num() && i < 5; ++i)
	{
		const FKey NumKey = (i == 0 ? EKeys::One : i == 1 ? EKeys::Two :
		                     i == 2 ? EKeys::Three : i == 3 ? EKeys::Four : EKeys::Five);
		if (K == NumKey && ChoiceButtons[i]->GetIsEnabled())
		{
			SelectedIndex = i;
			HighlightChoice(SelectedIndex);
			MakeChoice(i);
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UEclipseDialogueWidget::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
	// Re-take focus so keyboard nav keeps working even if user clicks the world
	if (IsVisible())
	{
		SetKeyboardFocus();
	}
	Super::NativeOnFocusLost(InFocusEvent);
}

void UEclipseDialogueWidget::MakeChoice(int32 Index)
{
	using namespace EclipseUI;

	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueChoiceSound);
	}

	// Stamp a player-side bubble with the chosen line BEFORE dispatching to
	// the subsystem — once MakeChoice returns the dialogue has already
	// advanced and CurrentChoices points at the next node's options. Player
	// bubbles share the same RightHistoryScroll as the NPC's, just with
	// bAlignRight=false so they hug the left edge of the column (DE-style
	// "YOU" indent). The line is fully resolved instantly — no per-word
	// fade — so it reads as something the player just committed to,
	// distinct from the cascading NPC reply.
	if (RightHistoryScroll && CurrentChoices.IsValidIndex(Index))
	{
		const FEclipseDialogueChoice& Picked = CurrentChoices[Index];
		UWrapBox* Words = AppendBubble(
			RightHistoryScroll,
			NSLOCTEXT("Eclipse", "PlayerSpeakerCaption", "YOU"),
			Cream,                          // player caption tint
			/*bAlignRight=*/false,
			/*EffectsOut=*/nullptr);
		if (Words && WidgetTree)
		{
			// Single static UTextBlock — no per-word stagger; instant.
			UTextBlock* Line = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), NAME_None);
			Line->SetFont(MakeRodin(/*Size=*/16));
			Line->SetColorAndOpacity(FSlateColor(Cream));
			Line->SetAutoWrapText(true);
			Line->SetText(Picked.Text);
			Words->AddChildToWrapBox(Line);
		}
	}

	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
		DS->MakeChoice(Index);
}

void UEclipseDialogueWidget::OnCloseClicked()
{
	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
		DS->CloseDialogue();
}

// CSlot callbacks
void UEclipseDialogueWidget::OnChoice0() { MakeChoice(0); }
void UEclipseDialogueWidget::OnChoice1() { MakeChoice(1); }
void UEclipseDialogueWidget::OnChoice2() { MakeChoice(2); }
void UEclipseDialogueWidget::OnChoice3() { MakeChoice(3); }
void UEclipseDialogueWidget::OnChoice4() { MakeChoice(4); }

// ─────────────────────────────────────────────────────────────
//  Word-by-word fade-in body animation
//
//  Splits the body string on whitespace and constructs one UTextBlock per
//  word inside BodyWords (a UWrapBox). Each block starts at alpha 0 and
//  fades to 1 over WordFadeDuration, with each successive word delayed by
//  WordSpawnInterval so the line cascades in left-to-right.
// ─────────────────────────────────────────────────────────────

void UEclipseDialogueWidget::StartBodyAnimation(const FString& BodyString)
{
	using namespace EclipseUI;

	if (!BodyWords || !WidgetTree) return;

	// Wipe previous run — also wipes any choice-row words from the prior node.
	BodyWords->ClearChildren();
	AnimWordBlocks.Reset();
	AnimWordDelays.Reset();
	AnimWordTints.Reset();
	AnimWordMumbleFired.Reset();

	// Cut any in-flight mumble slices from the previous node (user may have
	// clicked a choice mid-cascade, leaving slices ringing). Quick fade.
	for (TWeakObjectPtr<UAudioComponent>& Weak : ActiveMumbleSlices)
	{
		if (UAudioComponent* Live = Weak.Get())
		{
			Live->FadeOut(0.06f, 0.f);
		}
	}
	ActiveMumbleSlices.Reset();

	// ParseIntoArrayWS splits on any whitespace and discards empty entries.
	TArray<FString> Words;
	BodyString.ParseIntoArrayWS(Words);

	// Pick up the designer's font from the WBP-bound BodyText so per-word
	// blocks inherit whatever the designer set (e.g. Pantasia) rather than
	// being hard-locked to the C++ default. Falls back to MakeRodin(18) only
	// if BodyText isn't available — i.e. on the C++ fallback layout.
	const FSlateFontInfo BodyFont = BodyText
		? BodyText->GetFont()
		: MakeRodin(/*Size=*/18);

	for (int32 i = 0; i < Words.Num(); ++i)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("DlgWord_%d"), i)));

		// Trailing space is part of the word so the wrap-box can break naturally.
		Block->SetText(FText::FromString(Words[i] + TEXT(" ")));
		Block->SetFont(BodyFont);

		// Start fully transparent — NativeTick will lerp this up.
		FLinearColor Start = Cream; Start.A = 0.f;
		Block->SetColorAndOpacity(FSlateColor(Start));

		if (UWrapBoxSlot* WS = BodyWords->AddChildToWrapBox(Block))
		{
			// Tiny vertical breathing so descenders don't kiss the next line.
			WS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
		}

		AnimWordBlocks.Add(Block);
		AnimWordDelays.Add(static_cast<float>(i) * WordSpawnInterval);
		AnimWordTints.Add(Cream);
		AnimWordMumbleFired.Add(false);
	}

	// Total time the body takes to fully resolve = last_word_delay + fade_duration.
	BodyAnimTotalTime = (Words.Num() > 0)
		? (static_cast<float>(Words.Num() - 1) * WordSpawnInterval + WordFadeDuration)
		: 0.f;

	DialogueAnimTime  = 0.f;
	bDialogueAnimating = AnimWordBlocks.Num() > 0;

	UE_LOG(LogEclipse, Log, TEXT("Dlg: StartBodyAnimation — %d words, total=%.2fs, anim=%s"),
		AnimWordBlocks.Num(), BodyAnimTotalTime, bDialogueAnimating ? TEXT("ON") : TEXT("OFF"));
}

void UEclipseDialogueWidget::AnimateChoiceText(UTextBlock* Label, int32 ChoiceIndex,
	const FString& Text, const FLinearColor& TargetTint, float StartDelay)
{
	using namespace EclipseUI;
	if (!Label || !WidgetTree) return;

	UPanelWidget* Parent = Label->GetParent();
	if (!Parent) return;

	// Hide the static label — the wrap box of words will replace its visual role.
	Label->SetText(FText::GetEmpty());
	Label->SetVisibility(ESlateVisibility::Collapsed);

	// Find/construct a wrap box parked next to the label. Re-using lets the
	// WrapBox persist between dialogue nodes so we don't churn allocations.
	const FName WBName(*FString::Printf(TEXT("ChoiceWords_%d"), ChoiceIndex));
	UWrapBox* WB = Cast<UWrapBox>(WidgetTree->FindWidget(WBName));
	if (!WB)
	{
		WB = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), WBName);
		Parent->AddChild(WB);
		const int32 LabelIdx = Parent->GetChildIndex(Label);
		Parent->ShiftChild(FMath::Max(0, LabelIdx), WB);

		// HBox slot tweaks: take the remaining horizontal space, top-align so
		// long wrapped choices don't drift off-centre relative to the circle.
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(WB->Slot))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetVerticalAlignment(VAlign_Center);
		}
	}
	WB->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WB->ClearChildren();

	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);

	// Per-word blocks inherit the designer's font from the WBP-bound choice
	// label (e.g. ChoiceText_0..4) so styling stays where the designer sees
	// it. Falls back to MakeRodin(15) only if Label has no font set.
	const FSlateFontInfo ChoiceFont = Label
		? Label->GetFont()
		: MakeRodin(/*Size=*/15);

	for (int32 wi = 0; wi < Words.Num(); ++wi)
	{
		UTextBlock* W = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceWord_%d_%d"), ChoiceIndex, wi)));
		W->SetText(FText::FromString(Words[wi] + TEXT(" ")));
		W->SetFont(ChoiceFont);

		FLinearColor Start = TargetTint; Start.A = 0.f;
		W->SetColorAndOpacity(FSlateColor(Start));
		WB->AddChildToWrapBox(W);

		AnimWordBlocks.Add(W);
		AnimWordDelays.Add(StartDelay + static_cast<float>(wi) * WordSpawnInterval);
		AnimWordTints.Add(TargetTint);
		AnimWordMumbleFired.Add(false);
	}

	bDialogueAnimating = true;
}

void UEclipseDialogueWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);

	if (!bDialogueAnimating) return;

	DialogueAnimTime += DeltaSeconds;

	bool bAllDone = true;

	// ── Per-choice button reveal ──────────────────────────────────────────
	// Each button stays Collapsed (zero layout) until the body finishes and
	// its own staggered delay is reached; then it pops to Visible and the
	// per-word fade inside it starts running.
	for (int32 i = 0; i < ChoiceRevealButtons.Num(); ++i)
	{
		UButton* B = ChoiceRevealButtons[i];
		if (!B) continue;
		const float RevealAt = ChoiceRevealDelays.IsValidIndex(i) ? ChoiceRevealDelays[i] : 0.f;
		if (B->GetVisibility() == ESlateVisibility::Collapsed && DialogueAnimTime >= RevealAt)
		{
			B->SetVisibility(ESlateVisibility::Visible);
		}
		// Keep the animation flag alive while there are still hidden rows
		// pending — even if every word block is currently transparent and
		// counted as "not done" by the loop below, this guards against a
		// short body that finishes its words before the choice reveal time.
		if (DialogueAnimTime < RevealAt) bAllDone = false;
	}

	// ── Per-word fade + slide-up ──────────────────────────────────────────
	// Each word fades from alpha 0 → 1 AND rises ~6 px to its resting
	// position. Both curves use cubic ease-out so the motion settles softly
	// instead of stopping abruptly — much smoother read than the previous
	// linear ramp + zero-translation.
	const int32 N = AnimWordBlocks.Num();
	const float SlideUpPx = 6.f;
	for (int32 i = 0; i < N; ++i)
	{
		UTextBlock* Block = AnimWordBlocks[i];
		if (!Block) continue;

		const float Delay  = (AnimWordDelays.IsValidIndex(i) ? AnimWordDelays[i] : 0.f);
		const FLinearColor Tint = (AnimWordTints.IsValidIndex(i) ? AnimWordTints[i] : EclipseUI::Cream);
		const float tRaw = (DialogueAnimTime - Delay) / FMath::Max(0.0001f, WordFadeDuration);
		const float tLin = FMath::Clamp(tRaw, 0.f, 1.f);

		// Cubic ease-out: 1 - (1-t)^3. Eye-pleasing for short fades.
		const float Eased = 1.f - FMath::Pow(1.f - tLin, 3.f);

		FLinearColor C = Tint; C.A = Eased;
		Block->SetColorAndOpacity(FSlateColor(C));

		// Slide-up: start `SlideUpPx` below resting (positive Y in Slate
		// is downward), translate toward 0 as alpha fills in.
		const float YOffset = SlideUpPx * (1.f - Eased);
		Block->SetRenderTranslation(FVector2D(0.f, YOffset));

		// Leading edge: the moment a word's delay is crossed, splice off a
		// random mumble slice — but only every Nth word so the mumble feels
		// like phrases rather than chatter. AnimWordMumbleFired keeps it
		// strictly one-shot per word so we don't retrigger across frames.
		if (tRaw > 0.f && AnimWordMumbleFired.IsValidIndex(i) && !AnimWordMumbleFired[i])
		{
			AnimWordMumbleFired[i] = true;
			const int32 Stride = FMath::Max(1, MumbleWordsPerSlice);
			if (i % Stride == 0)
			{
				PlayMumbleSlice();
			}
		}

		if (Eased < 1.f) bAllDone = false;
	}

	if (bAllDone)
	{
		bDialogueAnimating = false;

		// Cut the voice off the moment text animation finishes. Without this
		// the last slice would keep ringing past the visible body. We use a
		// quick fade (60ms) rather than a hard stop to avoid clicks.
		for (TWeakObjectPtr<UAudioComponent>& Weak : ActiveMumbleSlices)
		{
			if (UAudioComponent* Live = Weak.Get())
			{
				Live->FadeOut(/*FadeOut=*/0.06f, /*FadeVolumeLevel=*/0.f);
			}
		}
		ActiveMumbleSlices.Reset();
	}
}

void UEclipseDialogueWidget::PlayMumbleSlice()
{
	if (!DialogueMumbleSound)
	{
		UE_LOG(LogEclipse, Verbose, TEXT("Dlg: PlayMumbleSlice — DialogueMumbleSound null, skipping"));
		return;
	}

	UEclipseAudioSubsystem* Audio = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>()
		: nullptr;
	if (!Audio)
	{
		UE_LOG(LogEclipse, Warning, TEXT("Dlg: PlayMumbleSlice — AudioSubsystem missing"));
		return;
	}

	// Slice duration — random within [Min, Max] for organic pacing, but the
	// START time is sequential so the underlying melody surfaces. Each slice
	// picks up where the previous one ended; when we'd overrun the end of the
	// clip we wrap back to 0.
	const float MinD = FMath::Max(0.01f, MumbleSliceMinSeconds);
	const float MaxD = FMath::Max(MinD,  MumbleSliceMaxSeconds);
	const float Duration = FMath::FRandRange(MinD, MaxD);

	const float SrcLen   = FMath::Max(0.f, MumbleSourceLength);
	const float StartTime = MumbleCursor;

	// Advance the cursor for next time. Wrap to 0 if the next slice (assuming
	// max-length) wouldn't fit before the end of the source.
	MumbleCursor += Duration;
	if (SrcLen > 0.f && (MumbleCursor + MaxD) > SrcLen)
	{
		MumbleCursor = 0.f;
	}

	// Pitch — defaults are 1.0/1.0 so the melody plays straight. If the
	// designer widens the range we still apply per-slice randomness.
	const float Pitch = (FMath::IsNearlyEqual(MumblePitchMin, MumblePitchMax))
		? MumblePitchMin
		: FMath::FRandRange(MumblePitchMin, MumblePitchMax);

	UAudioComponent* C = Audio->PlaySliced(DialogueMumbleSound, StartTime, Duration,
		Pitch, MumbleVolume, MumbleSliceFadeOutSeconds);

	// Track the live slice so the moment dialogue animation finishes we can
	// cut it off (otherwise the in-flight slice keeps playing past the visible
	// text, which feels wrong — voice should stop when the text is done).
	if (C)
	{
		ActiveMumbleSlices.Add(C);
	}

	UE_LOG(LogEclipse, Verbose, TEXT("Dlg: mumble — start=%.2f dur=%.2f pitch=%.2f fade=%.2f cursor->%.2f"),
		StartTime, Duration, Pitch, MumbleSliceFadeOutSeconds, MumbleCursor);
}
