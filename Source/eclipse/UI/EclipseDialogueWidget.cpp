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
#include "Components/SizeBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ButtonSlot.h"
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
			// Right third of the screen, full height (anchor-stretched so the
			// box tracks viewport width instead of a fixed pixel size).
			CSlot->SetAnchors(FAnchors(2.f / 3.f, 0.f, 1.f, 1.f));
			CSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 0.f));
		}

		// ── Vertical column inside panel: speaker, body, choices ───────────
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("DialogueColumn"));
		Panel->SetContent(Column);

		// Speaker name (BMSPA, cyan)
		SpeakerNameText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("SpeakerNameText"));
		SpeakerNameText->SetFont(MakeBMSPA(/*Size=*/15, /*Letter=*/3.f));
		SpeakerNameText->SetColorAndOpacity(FSlateColor(DialogueRed));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(SpeakerNameText))
			S->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f));

		// Body text (RodinPro, cream)
		BodyText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("BodyText"));
		BodyText->SetFont(MakeRodin(/*Size=*/14));
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

		// (No close button — dialogue exits only through dialogue options
		// like [Leave] / [Goodbye].)
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
			// Fully transparent — no panel behind the transcript. Individual
			// closed-caption sentence boxes (BeginSentenceBox) carry their
			// own black background now, so the captions read as if printed
			// straight over the game world, not inside a UI panel.
			OuterBox->SetBrush(RoundedBrush(
				FLinearColor::Transparent, FLinearColor::Transparent, 0.f, 8.f));
			OuterBox->SetPadding(FMargin(16.f, 14.f));
			if (UCanvasPanelSlot* CS = RootCanvas->AddChildToCanvas(OuterBox))
			{
				// Right third of the viewport (anchor-stretched). Top/bottom
				// anchors and right margin are Class-Defaults-editable (see
				// BoxTopAnchor/BoxBottomAnchor/BoxRightMargin) so the box can
				// be resized from the WBP without a C++ change.
				CS->SetAnchors(FAnchors(2.f / 3.f, BoxTopAnchor, 1.f, BoxBottomAnchor));
				CS->SetOffsets(FMargin(0.f, 0.f, BoxRightMargin, 0.f));
				CS->SetZOrder(1);
			}

			UVerticalBox* OuterCol = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("DialogueOuterColumn"));
			OuterBox->SetContent(OuterCol);

			RightHistoryScroll = WidgetTree->ConstructWidget<UScrollBox>(
				UScrollBox::StaticClass(), TEXT("RightHistoryScroll_Runtime"));
			// Never let the scrollbox consume the wheel event itself — every
			// wheel tick, anywhere over the panel, bubbles up to this
			// widget's NativeOnMouseWheel instead, so there's exactly one
			// scroll path (consistent speed, easing, pixel-snapping) rather
			// than the native in-box behaviour and a fallback disagreeing.
			RightHistoryScroll->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
			RightHistoryScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
			RightHistoryScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			{
				// Kill the built-in top/bottom "more content" shadow gradients —
				// they read as a stray horizontal line at the box's edges once
				// the transcript overflows and starts scrolling.
				FScrollBoxStyle NoShadowStyle = RightHistoryScroll->GetWidgetStyle();
				NoShadowStyle.TopShadowBrush    = FSlateNoResource();
				NoShadowStyle.BottomShadowBrush = FSlateNoResource();
				RightHistoryScroll->SetWidgetStyle(NoShadowStyle);
			}
			if (UVerticalBoxSlot* VS = OuterCol->AddChildToVerticalBox(RightHistoryScroll))
			{
				// Fill the available vertical space so the scroll grows with
				// the panel; choices below get whatever height they need.
				VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}

			UE_LOG(LogEclipse, Log,
				TEXT("Dlg: DialogueOuterBox injected (right third, 90%% height)"));
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

			// 1b. Re-anchor the box + legacy panel to the RIGHT THIRD of the
			//     viewport, whether they came from the WBP bake (fixed 480/500px
			//     right-anchored) or the runtime injection above. Idempotent —
			//     anchor-stretched so the box tracks viewport width.
			auto AnchorRightThird = [this](const TCHAR* Name, float Top, float Bottom, float RightMargin)
			{
				if (UWidget* W = WidgetTree->FindWidget(FName(Name)))
				{
					if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot))
					{
						CS->SetAnchors(FAnchors(2.f / 3.f, Top, 1.f, Bottom));
						CS->SetOffsets(FMargin(0.f, 0.f, RightMargin, 0.f));
					}
				}
			};
			AnchorRightThird(TEXT("DialogueOuterBox"), BoxTopAnchor, BoxBottomAnchor, BoxRightMargin);
			AnchorRightThird(TEXT("DialoguePanel"),    0.f,   1.f,   0.f);

			// 1c. Portrait moved to the TOP-LEFT of the screen (was flush
			//     against the transcript box's left edge) — a compact HUD
			//     corner avatar next to the speaker's name, matching a
			//     classic VN name-tag layout. Outline stays red.
			constexpr float PortraitW = 100.f, PortraitH = 140.f, CornerMargin = 24.f;
			if (UWidget* P = WidgetTree->FindWidget(TEXT("SpeakerPortraitSize")))
			{
				if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(P->Slot))
				{
					CS->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
					CS->SetAlignment(FVector2D(0.f, 0.f));
					CS->SetPosition(FVector2D(CornerMargin, CornerMargin));
					CS->SetSize(FVector2D(PortraitW, PortraitH));
					CS->SetZOrder(4);
				}
			}
			if (USizeBox* PSize = Cast<USizeBox>(WidgetTree->FindWidget(TEXT("SpeakerPortraitSize"))))
			{
				PSize->SetWidthOverride(PortraitW);
				PSize->SetHeightOverride(PortraitH);
			}
			if (SpeakerPortrait)
			{
				// Rebuild the baked brush with a red outline. Mirrors the
				// populator's navy-fill RoundedBox; SetBrushFromTexture later
				// only swaps the resource, so the outline survives.
				FSlateBrush PB;
				PB.DrawAs    = ESlateBrushDrawType::RoundedBox;
				PB.TintColor = FSlateColor(FLinearColor(0.078f, 0.169f, 0.314f, 1.f));
				PB.OutlineSettings.CornerRadii  = FVector4(6, 6, 6, 6);
				PB.OutlineSettings.Color        = FSlateColor(DialogueRed);
				PB.OutlineSettings.Width        = 2.f;
				PB.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
				PB.ImageSize = FVector2D(PortraitW, PortraitH);
				SpeakerPortrait->SetBrush(PB);
			}
		}

		// 2. Collapse the in-column body / effects widgets.
		//    Each new dialogue line is now a self-contained bubble appended
		//    to LeftHistoryScroll / RightHistoryScroll — these in-column
		//    instances render nothing and just take up space. SpeakerNameText
		//    is collapsed too — the per-bubble caption (AppendBubble) already
		//    shows the speaker's name inline above each row, so a second,
		//    separately-positioned name tag was pure duplication (and, once
		//    reparented onto the root canvas at a fixed corner position,
		//    would linger on screen behind other full-screen menus like the
		//    Stats UI instead of closing/hiding with the rest of the panel).
		//    BodyWords is REPOINTED in HandleNodeChanged (not collapsed here)
		//    because StartBodyAnimation drives it; collapsing the in-column
		//    WrapBox is fine because we redirect the pointer before the
		//    first node arrives.
		if (SpeakerNameText)  SpeakerNameText->SetVisibility(ESlateVisibility::Collapsed);
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

		// 5. ChoicesBox no longer gets parked once below the transcript —
		//    HandleNodeChanged now re-parents it into RightHistoryScroll
		//    itself, as the last child right after the newest NPC row, every
		//    turn. That keeps the options directly under whatever's most
		//    recently printed instead of pinned to a fixed screen position,
		//    and lets the player's next line + the following NPC line just
		//    continue the same scrolling column once a choice is made
		//    (MakeChoice collapses ChoicesBox rather than removing it, so
		//    the player's row lands exactly where the options were).
	}
	#endif // bubble runtime override

	// Hidden until a dialogue opens
	SetVisibility(ESlateVisibility::Collapsed);

	// Close-X retired: dialogue exits only through dialogue options
	// ([Leave] / [Goodbye] — every leaf node auto-offers one). Collapse
	// any CloseButton still baked into an older WBP.
	if (CloseButton)
	{
		CloseButton->SetVisibility(ESlateVisibility::Collapsed);
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
//  Closed-caption row construction
//
//  AppendBubble builds one caption row — a UVerticalBox of speaker-caption
//  label + UWrapBox of per-SENTENCE black caption boxes (BeginSentenceBox)
//  + optional effects line — and appends it to the supplied UScrollBox. No
//  chat-bubble container, no per-row background, no tail: each SENTENCE
//  carries its own black box (grouping its words, not one box per word),
//  the row itself is bare. The wrap box is returned so the caller can hook
//  the word reveal (HandleNodeChanged repoints `BodyWords` here so
//  StartBodyAnimation lands in the new row).
//
//  WrapSize is set explicitly so long lines wrap within the transcript
//  column instead of pushing boxes off the visible scroll area.
// ─────────────────────────────────────────────────────────────

TArray<TArray<FString>> UEclipseDialogueWidget::SplitIntoSentences(const FString& Text)
{
	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);

	auto EndsSentence = [](const FString& Word) -> bool
	{
		FString W = Word;
		// Tolerate a trailing quote/paren after the mark, e.g. `crying."`.
		while (W.Len() > 0)
		{
			const TCHAR C = W[W.Len() - 1];
			if (C == TEXT('"') || C == TEXT('\'') || C == TEXT(')') || C == TEXT(']'))
			{
				W.LeftChopInline(1);
				continue;
			}
			break;
		}
		if (W.IsEmpty()) return false;
		const TCHAR Last = W[W.Len() - 1];
		return Last == TEXT('.') || Last == TEXT('!') || Last == TEXT('?');
	};

	TArray<TArray<FString>> Sentences;
	TArray<FString> Current;
	for (const FString& Word : Words)
	{
		Current.Add(Word);
		if (EndsSentence(Word))
		{
			Sentences.Add(MoveTemp(Current));
			Current.Reset();
		}
	}
	if (Current.Num() > 0)
	{
		Sentences.Add(MoveTemp(Current));
	}
	return Sentences;
}

UEclipseDialogueWidget::FSentenceBox UEclipseDialogueWidget::BeginSentenceBox(UWrapBox* Parent, float WrapWidth,
	int32 SentenceIndex, bool bIsFirst, bool bIsLast, bool bAlignRight)
{
	using namespace EclipseUI;
	FSentenceBox Result;
	if (!WidgetTree || !Parent) return Result;

	// The visible box is only trimmed by enough to leave room for the small
	// per-line nudge below — it should read as a continuous block of text,
	// not distinct floating boxes.
	const float VisibleCap = FMath::Max(120.f, WrapWidth - 24.f);

	UWrapBox* Inner = WidgetTree->ConstructWidget<UWrapBox>(
		UWrapBox::StaticClass(), NAME_None);
	Inner->SetInnerSlotPadding(FVector2D(0.f, 0.f));
	Inner->SetExplicitWrapSize(true);
	// Subtract the box's own horizontal padding so wrapped text doesn't
	// press against — or overflow past — the black background.
	Inner->SetWrapSize(FMath::Max(80.f, VisibleCap - 32.f));

	UBorder* Box = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), NAME_None);

	// Square off whichever corners touch a neighbouring box (InnerSlotPadding
	// between sentence boxes is 0 — see AppendBubble) so the stack reads as
	// one continuous joined strip; only the outermost edges stay rounded.
	FSlateBrush Brush;
	Brush.DrawAs    = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.80f));
	Brush.OutlineSettings.Color = FSlateColor(FLinearColor::Transparent);
	Brush.OutlineSettings.Width = 0.f;
	const float TopR    = bIsFirst ? 8.f : 0.f;
	const float BottomR = bIsLast  ? 8.f : 0.f;
	Brush.OutlineSettings.CornerRadii  = FVector4(TopR, TopR, BottomR, BottomR); // TL,TR,BR,BL
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	Box->SetBrush(Brush);

	Box->SetPadding(FMargin(16.f, 10.f));
	Box->SetContent(Inner);
	Box->SetVisibility(ESlateVisibility::Collapsed);   // revealed by the caller

	// Reserve a full-row SizeBox so the next sentence is still forced onto a
	// new line. The visible Box stays pinned to the row's speaker-side edge
	// (left for NPC, right for the player) — no left/right swing — but every
	// other sentence gets a small nudge (~1-2 letters) further in from that
	// edge, just enough to read as a subtle stagger on a continuous block.
	const float Nudge = (SentenceIndex % 2 == 1) ? 14.f : 0.f;
	USizeBox* Slot = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), NAME_None);
	Slot->SetWidthOverride(WrapWidth);
	Slot->SetContent(Box);
	if (USizeBoxSlot* SBS = Cast<USizeBoxSlot>(Box->Slot))
	{
		SBS->SetHorizontalAlignment(bAlignRight ? HAlign_Right : HAlign_Left);
		SBS->SetVerticalAlignment(VAlign_Top);
		SBS->SetPadding(bAlignRight ? FMargin(0.f, 0.f, Nudge, 0.f) : FMargin(Nudge, 0.f, 0.f, 0.f));
	}

	if (UWrapBoxSlot* WS = Parent->AddChildToWrapBox(Slot))
	{
		WS->SetHorizontalAlignment(HAlign_Fill);
	}

	Result.Box   = Box;
	Result.Inner = Inner;
	return Result;
}

UWrapBox* UEclipseDialogueWidget::AppendBubble(UScrollBox* Box,
	const FText& SpeakerCaption, const FLinearColor& CaptionTint,
	UTextBlock** EffectsOut, bool bAlignRight)
{
	using namespace EclipseUI;
	if (!Box || !WidgetTree) return nullptr;

	UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), NAME_None);
	Col->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Speaker caption — BMSPA label above the caption boxes (red for NPC,
	// cream "YOU" for the player; caller passes the colour). This is the
	// only per-speaker visual distinction now — caption boxes render the
	// same black-on-white for both sides, matching real closed captions.
	UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), NAME_None);
	Caption->SetFont(MakeBMSPA(/*Size=*/13, /*Letter=*/2.f));
	Caption->SetColorAndOpacity(FSlateColor(CaptionTint));
	Caption->SetText(SpeakerCaption);
	Caption->SetJustification(bAlignRight ? ETextJustify::Right : ETextJustify::Left);
	if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(Caption))
		S->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	// WrapBox of per-SENTENCE caption boxes (BeginSentenceBox). Explicit
	// wrap so long lines stack rows within the transcript column. Zero gap
	// so consecutive sentence boxes touch — bottom of one flush against the
	// top of the next, joining the stack into one strip (see the squared-off
	// touching corners + left/right stagger in BeginSentenceBox).
	UWrapBox* Words = WidgetTree->ConstructWidget<UWrapBox>(
		UWrapBox::StaticClass(), NAME_None);
	Words->SetInnerSlotPadding(FVector2D(0.f, 0.f));
	Words->SetExplicitWrapSize(true);
	// Wrap relative to the (anchor-stretched) transcript column so rows use
	// the right-third box's width. Cached geometry is zero on the very
	// first frames after opening — fall back to a safe fixed wrap there.
	float WrapW = 400.f;
	{
		const float LocalW = Box->GetCachedGeometry().GetLocalSize().X;
		if (LocalW > 120.f) WrapW = LocalW - 24.f;
	}
	Words->SetWrapSize(WrapW);
	Col->AddChildToVerticalBox(Words);

	// Optional effects line under the words. Collapsed by default — the
	// caller sets text + flips visibility if the node has any stage
	// directives.
	if (EffectsOut)
	{
		UTextBlock* EffectsBlk = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		EffectsBlk->SetFont(MakeRodin(13));
		EffectsBlk->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.65f, 0.25f, 0.95f)));
		EffectsBlk->SetAutoWrapText(true);
		EffectsBlk->SetVisibility(ESlateVisibility::Collapsed);
		if (UVerticalBoxSlot* S = Col->AddChildToVerticalBox(EffectsBlk))
			S->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f));
		*EffectsOut = EffectsBlk;
	}

	// NPC lines hug the left edge of the transcript; the player's own lines
	// are pushed to the right edge (bAlignRight) — a clear at-a-glance cue
	// for who's speaking that survives even with the near-full-width box
	// stagger.
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(Box->AddChild(Col)))
	{
		SS->SetHorizontalAlignment(bAlignRight ? HAlign_Right : HAlign_Left);
		SS->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}

	// Auto-scroll so the newest row is always visible. Clears any pending
	// wheel-scroll lerp target too — otherwise NativeTick would immediately
	// drag the view back toward a now-stale target on the next frame.
	Box->ScrollToEnd();
	if (Box == RightHistoryScroll) bDialogueScrollTargetActive = false;

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

	// Swap in the speaker portrait if one is assigned on the NPC. Turn-based
	// show/hide (HandleNodeChanged / MakeChoice) only re-shows it when
	// bSpeakerHasPortrait is true, so an NPC with no portrait never flashes
	// an empty frame on their turn.
	bSpeakerHasPortrait = (Npc && Npc->PortraitTexture != nullptr);
	if (SpeakerPortrait)
	{
		if (bSpeakerHasPortrait)
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
	// MakeChoice sets bPlayerLineAnimating and kicks off the "YOU" cascade
	// BEFORE calling into the subsystem, and the subsystem advances +
	// broadcasts this synchronously on the same call stack — so at this
	// point the player's line is always still mid-reveal. Hold the NPC's
	// turn back until NativeTick sees the player's line finish.
	if (bPlayerLineAnimating)
	{
		PendingNode = Node;
		PendingNodeTimer = 0.f;
		return;
	}
	ApplyNodeChanged(Node);
}

void UEclipseDialogueWidget::ApplyNodeChanged(const FEclipseDialogueNodeView& Node)
{
	// New node, new options — the player may choose again.
	bChoiceCommitted = false;
	using namespace EclipseUI;

	// SelfHitTestInvisible: the root canvas itself ignores hit-testing so only
	// the actual buttons/panel intercept input. Without this, the fullscreen
	// canvas would be a hit-test layer over the whole viewport, producing
	// off-centre hover/click registration on the buttons inside.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// This fires on every NPC line — i.e. the start of the NPC's "turn".
	// Re-show the portrait (hidden by MakeChoice during the player's turn);
	// it only re-appears if this NPC actually has one (bSpeakerHasPortrait,
	// set in HandleDialogueOpened). SpeakerNameText itself stays permanently
	// collapsed — the per-bubble caption already shows the speaker's name.
	if (SpeakerPortrait && bSpeakerHasPortrait)
	{
		SpeakerPortrait->SetVisibility(ESlateVisibility::HitTestInvisible);
	}

	// Append a new NPC caption row and redirect the per-word animation +
	// effects-line pointers so the existing machinery lands in the new row.
	// The old WrapBox stays where it is in the prior row (in the scroll
	// box) with its chips intact — the history accumulates.
	if (RightHistoryScroll)
	{
		UTextBlock* BubbleEffects = nullptr;
		UWrapBox* BubbleWords = AppendBubble(
			RightHistoryScroll,
			FText::FromName(Node.SpeakerName),
			DialogueRed,                   // NPC caption tint (red accent)
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

	// Move ChoicesBox to the very end of the transcript scroll, right after
	// the NPC row just appended above — so the options always sit directly
	// under the last line printed, not pinned to a fixed spot on screen.
	// Same UVerticalBox instance every turn (never destroyed), just
	// detached and re-added at the new end; RebuildChoices (below) then
	// fills it with this node's rows.
	if (RightHistoryScroll && ChoicesBox)
	{
		ChoicesBox->RemoveFromParent();
		if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(RightHistoryScroll->AddChild(ChoicesBox)))
		{
			SS->SetHorizontalAlignment(HAlign_Fill);
			SS->SetPadding(FMargin(0.f, 4.f, 0.f, 160.f));
		}
		ChoicesBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
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
			EffectsLineText->SetFont(EclipseUI::MakeRodin(13));
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
	Line->SetFont(EclipseUI::MakeBMSPA(/*Size=*/11, /*Letter=*/1.5f));
	Line->SetColorAndOpacity(FSlateColor(XPGreen));
	Line->SetText(FText::FromString(Msg));

	// Naked line (no chip), left-aligned under the player's chosen line.
	// Appears immediately — no fade.
	if (UScrollBoxSlot* SS = Cast<UScrollBoxSlot>(RightHistoryScroll->AddChild(Line)))
	{
		SS->SetHorizontalAlignment(HAlign_Left);
		SS->SetPadding(FMargin(6.f, 0.f, 0.f, 10.f));
	}
	RightHistoryScroll->ScrollToEnd();
	bDialogueScrollTargetActive = false;
}

void UEclipseDialogueWidget::HandleDialogueClosed()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (SpeakerNameText)
	{
		SpeakerNameText->SetText(FText::GetEmpty());
		SpeakerNameText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (BodyText)        BodyText->SetText(FText::GetEmpty());
	if (SpeakerPortrait) SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);
	bSpeakerHasPortrait = false;

	// Clear the bubble transcript so the next conversation starts fresh —
	// otherwise the previous NPC's lines would still be stacked when the
	// player walks up to a different character. Same for the player-side
	// scroll.
	if (LeftHistoryScroll)  LeftHistoryScroll->ClearChildren();
	if (RightHistoryScroll) RightHistoryScroll->ClearChildren();
	CurrentChoices.Reset();
	ChoiceBaseTints.Reset();
	ChoiceLabelWordCounts.Reset();
	ChoiceRowBackgrounds.Reset();
	ChoiceLabelWidgets.Reset();
	ChoicePrefixLabels.Reset();

	// Path A's ChoiceBtn_0..4 rows are persistent WBP widgets — never
	// destroyed between dialogues — so each slot's WrapBox stays physically
	// attached to its row even after we stop tracking it here. Resetting the
	// array alone orphaned those widgets in place (still parented, still
	// showing whatever text they last had) while AnimateChoiceText, seeing
	// an "empty" ChoiceWordBoxes on the next dialogue, constructed a brand
	// new WrapBox alongside the old one — the two rendered stacked in the
	// same row, which is why a stale "[Goodbye]" (or two, across repeated
	// close/reopen cycles) could sit above the current, correct choice text.
	// Detach before dropping the tracking array so no orphan is left behind.
	for (UWrapBox* WB : ChoiceWordBoxes)
	{
		if (WB) WB->RemoveFromParent();
	}
	ChoiceWordBoxes.Reset();
	PendingNode.Reset();

	// Tear down the per-word animation state so we don't keep ticking dead
	// references on the next NativeTick.
	bDialogueAnimating = false;
	DialogueAnimTime = 0.f;
	if (BodyWords) BodyWords->ClearChildren();
	AnimWordBlocks.Reset();
	AnimWordDelays.Reset();
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
//  Choice buttons — plain text rows, mirrors HTML .dialogue-choice
// ─────────────────────────────────────────────────────────────

namespace
{
	// Display text for a choice with the authored "[STAT:N]" prefix replaced
	// by a clean, readable requirement tag — e.g. "[AESTHETICS 3] I like the
	// outfit." The player now sees exactly what's being checked; the stat
	// colour coding on top of that (ChoiceTint) still shows how close they
	// are to clearing it.
	FString DisplayChoiceText(const FEclipseDialogueChoice& Choice)
	{
		FString S = Choice.Text.ToString();
		if (Choice.bIsSkillCheck && S.StartsWith(TEXT("[")))
		{
			int32 CloseIdx = INDEX_NONE;
			if (S.FindChar(TEXT(']'), CloseIdx))
			{
				S.RemoveAt(0, CloseIdx + 1);
				S.TrimStartInline();
			}
			S = FString::Printf(TEXT("[%s %d] %s"),
				*Choice.SkillCheckStat.ToString().ToUpper(), Choice.SkillCheckValue, *S);
		}
		if (Choice.bHasStatCheckLabel)
		{
			FString StatName = Choice.StatCheckLabelStat.ToString().ToLower();
			if (StatName.Len() > 0) StatName = StatName.Left(1).ToUpper() + StatName.Mid(1);
			S = FString::Printf(TEXT("%s [%d]: %s"), *StatName, Choice.StatCheckLabelValue, *S);
		}
		return S;
	}

	// The choice's words WITHOUT any skill-check label — "Do the thing"
	// rather than "Rhythm [1]: Do the thing".
	//
	// The check belongs on the button, where it tells you what you're
	// risking before you commit. Once you've committed it's noise: the
	// transcript is meant to read back as things people said, and nobody
	// said "Rhythm [1]". So the choice row uses DisplayChoiceText and the
	// echoed player line uses this.
	FString PlainChoiceText(const FEclipseDialogueChoice& Choice)
	{
		FString S = Choice.Text.ToString();
		// An explicit "[STAT:N] ..." marker is part of the raw text, so it
		// has to be cut off the front. The Ink-native "{stat > N}" gate
		// never appears in the text at all — DisplayChoiceText synthesises
		// that prefix — so there's nothing to strip for it.
		if (Choice.bIsSkillCheck && S.StartsWith(TEXT("[")))
		{
			int32 CloseIdx = INDEX_NONE;
			if (S.FindChar(TEXT(']'), CloseIdx))
			{
				S.RemoveAt(0, CloseIdx + 1);
				S.TrimStartInline();
			}
		}
		return S;
	}

	// Same text as DisplayChoiceText, but cut into the part that gets the
	// stat colour and the part that doesn't. OutPrefix is exactly the stat
	// name + threshold ("Aesthetics [3]" or "[AESTHETICS 3]") — the colon
	// and every following word stay in OutBody, neutral. OutPrefix is empty
	// for choices with no stat-check label, in which case OutBody is the
	// whole line.
	void SplitChoiceLabel(const FEclipseDialogueChoice& Choice, FString& OutPrefix, FString& OutBody)
	{
		OutPrefix.Reset();
		OutBody = Choice.Text.ToString();

		if (Choice.bIsSkillCheck && OutBody.StartsWith(TEXT("[")))
		{
			int32 CloseIdx = INDEX_NONE;
			if (OutBody.FindChar(TEXT(']'), CloseIdx))
			{
				OutBody.RemoveAt(0, CloseIdx + 1);
				OutBody.TrimStartInline();
			}
			OutPrefix = FString::Printf(TEXT("[%s %d]"),
				*Choice.SkillCheckStat.ToString().ToUpper(), Choice.SkillCheckValue);
			OutBody = TEXT(" ") + OutBody;
		}

		if (Choice.bHasStatCheckLabel)
		{
			FString StatName = Choice.StatCheckLabelStat.ToString().ToLower();
			if (StatName.Len() > 0) StatName = StatName.Left(1).ToUpper() + StatName.Mid(1);
			// Colon deliberately on the body side — the player asked for the
			// tag alone to carry the colour, punctuation included out.
			OutPrefix = FString::Printf(TEXT("%s [%d]"), *StatName, Choice.StatCheckLabelValue);
			OutBody = TEXT(": ") + OutBody.TrimStart();
		}
	}

	// How many leading whitespace-split words of DisplayChoiceText's output
	// are the stat-check label prefix, not the choice's own wording — both
	// prefix shapes ("[AESTHETICS 3]" and "Aesthetics [3]:") are exactly 2
	// tokens (stat names never contain spaces), so no need to re-parse text.
	int32 ChoiceLabelWordCount(const FEclipseDialogueChoice& Choice)
	{
		if (Choice.bHasStatCheckLabel) return 2;
		if (Choice.bIsSkillCheck && Choice.Text.ToString().StartsWith(TEXT("["))) return 2;
		return 0;
	}
}

FLinearColor UEclipseDialogueWidget::ChoiceTint(const FEclipseDialogueChoice& Choice) const
{
	using namespace EclipseUI;

	// The card behind a choice row is WHITE at rest, so the resting text is
	// black and every tint here has to read against white. (Hover flips the
	// card to black and NativeTick overrides the colour, so only the resting
	// state is this function's problem.)
	const FLinearColor ChoiceBase(0.f, 0.f, 0.f, 1.f);

	if (!Choice.bIsSkillCheck && !Choice.bHasStatCheckLabel)
	{
		return Choice.bAvailable
			? ChoiceBase
			: FLinearColor(0.62f, 0.14f, 0.11f, 1.f);   // red hint for blocked picks
	}

	// bHasStatCheckLabel choices (Ink-native "{stat > N}" gates) are always
	// bAvailable — Ink already filtered out the failing ones — so they only
	// ever take this stat-hue path, never the red "blocked" one above.
	const FName Stat = Choice.bIsSkillCheck ? Choice.SkillCheckStat : Choice.StatCheckLabelStat;

	// Per-stat hue, in the deep variant that reads on the white card (see
	// StatHueDeep — the StatHue pastels are for dark panels).
	const FLinearColor Hue = StatHueDeep(Stat).Get(ChoiceBase);

	// Colour-coding is the POINT of these rows — it's how the player reads
	// at a glance which stat a check leans on — so every stat-check option
	// shows its hue from level 1. Levelling only deepens it the last 15%,
	// so growth still registers without the low levels being unreadable.
	//
	// (This used to blend in from level 3 and clamp to 0 below that. Stats
	// start at 1, so in practice every check rendered plain black and
	// nothing was ever colour-coded.)
	int32 Level = 1;
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			Level = GS->GetStatValue(Stat);
		}
	}
	const float Blend = FMath::Clamp(0.85f + (static_cast<float>(Level) - 1.f) * 0.017f, 0.f, 1.f);
	return FMath::Lerp(ChoiceBase, Hue, Blend);
}

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

	// Path A (reusing the WBP's persistent ChoiceBtn_0..4) is disabled.
	// Those buttons repeatedly rendered stale or blank content even when
	// logging proved the widget tree, text, and visibility were all correct
	// on every turn — every targeted fix (fresh WrapBox, explicit
	// label/background hide+show, layout invalidation, dropping the word
	// animation, instant reveal) failed to make them paint reliably. Path B
	// below sidesteps them entirely: it clears ChoicesBox and constructs
	// brand-new buttons every call, so no widget ever persists across turns
	// and there is nothing left to hold stale paint state.
	// To re-enable Path A, restore this to `if (PreBtns[0] != nullptr)`.
	if (false)
	{
		ChoiceButtons.Reset();
		ChoiceBaseTints.Reset();
		ChoiceLabelWordCounts.Reset();
		ChoiceRowBackgrounds.Reset();
		ChoiceLabelWidgets.Reset();
		ChoiceRevealButtons.Reset();
		ChoiceRevealDelays.Reset();

		for (int32 i = 0; i < MaxSlots; ++i)
		{
			UButton*    Btn   = PreBtns[i];
			UTextBlock* OrigLabel = PreTexts[i];
			if (!Btn) continue;

			// Fire on press, not press+release — avoids the "feels like double-
			// click" symptom from UE's default DownAndUp ClickMethod when
			// FInputModeUIOnly first-click consumes focus and the second click
			// finally registers as the press.
			Btn->SetClickMethod(EButtonClickMethod::MouseDown);

			// The button chrome itself stays fully transparent in every state
			// — the visible card (white bg at rest, black bg on hover/select)
			// is the wrapping Border below; NativeTick's hover pass repaints
			// its brush, not the button's own style.
			{
				FButtonStyle Ghost;
				Ghost.Normal   = RoundedBrush(FLinearColor::Transparent, FLinearColor::Transparent, 0.f, 0.f);
				Ghost.Hovered  = Ghost.Normal;
				Ghost.Pressed  = Ghost.Normal;
				Ghost.Disabled = Ghost.Normal;
				Btn->SetStyle(Ghost);
			}
			// Row stretches to the full width of the dialogue box — both the
			// button in its column AND the content row inside the button.
			// UButtonSlot defaults to HAlign_Center, which shrink-wraps the
			// text once the button chrome is invisible; force Fill.
			if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Btn->Slot))
			{
				VS->SetHorizontalAlignment(HAlign_Fill);
			}

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

			// Discard whatever content this button currently has and rebuild
			// it fresh from C++ every call — don't reuse or try to hide the
			// WBP's own baked ChoiceRow_i/ChoiceBg_i/ChoiceText_i
			// sub-widgets. That reuse path proved unreliable across many
			// rounds of testing: design-time placeholder text and card
			// backgrounds kept bleeding through no matter how explicitly
			// they were cleared/collapsed, strongly suggesting this WBP's
			// actual saved hierarchy doesn't match what the reuse code
			// assumed. Only the button itself (ChoiceBtn_i) has been
			// reliable throughout — its clicks always mapped to the right
			// choice — so it's the only thing still trusted from the WBP.
			Btn->SetContent(nullptr);

			if (i < Choices.Num())
			{
				const FEclipseDialogueChoice& Choice = Choices[i];
				// Failed skill-check choices stay CLICKABLE so the player can
				// attempt them at an Energy cost — see DialogueSubsystem::MakeChoice.
				// Stage-directive gates (StatGate / ItemGate) genuinely block
				// the action, so those buttons are disabled.
				const bool bHasGateHint = !Choice.GateHint.IsEmpty();
				Btn->SetIsEnabled(!bHasGateHint);

				// Stripped display text — no "[STAT:N]" tag, no failure-cost
				// hint. Risky checks look like any other line; the outcome
				// teaches the player. Gate hints (blocked actions) stay.
				FString S = DisplayChoiceText(Choice);
				if (bHasGateHint)
				{
					S += TEXT("  ") + Choice.GateHint.ToString();
				}
				// Base text is plain black (card is white at rest) — NativeTick's
				// hover pass swaps both the card and the text to the opposite
				// scheme (black card / white text) on hover or keyboard select.
				const FLinearColor Tint = FLinearColor::Black;

				UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), NAME_None);
				RowBg->SetPadding(FMargin(10.f, 6.f));
				RowBg->SetBrush(RoundedBrush(FLinearColor::White, FLinearColor::Transparent, 0.f, 8.f));

				UTextBlock* NewLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), NAME_None);
				NewLabel->SetFont(OrigLabel ? OrigLabel->GetFont() : EclipseUI::MakeRodin(20));
				NewLabel->SetAutoWrapText(true);
				NewLabel->SetText(FText::FromString(S));
				NewLabel->SetColorAndOpacity(FSlateColor(Tint));
				RowBg->SetContent(NewLabel);
				Btn->SetContent(RowBg);
				if (UButtonSlot* NewBS = Cast<UButtonSlot>(RowBg->Slot))
				{
					NewBS->SetHorizontalAlignment(HAlign_Fill);
				}

				ChoiceBaseTints.Add(Tint);
				ChoiceRowBackgrounds.Add(RowBg);
				ChoiceLabelWidgets.Add(NewLabel);

				// LabelWordCount still feeds NativeTick's hover pass (skips
				// recolouring the stat-check label prefix on hover). The
				// stat-hue tint itself was only ever applied via the now-
				// removed per-word animation, so a static-label choice just
				// reads in plain Tint for now — a minor style regression
				// versus the animated version, not a functional one.
				const int32 LabelWordCount = ChoiceLabelWordCount(Choice);
				ChoiceLabelWordCounts.Add(LabelWordCount);

				// Choices now appear immediately rather than staggering in
				// over ~0.5-0.7s — the previous Collapsed-until-StartDelay
				// cascade (see git history) made a correctly-built, correctly
				// -revealed row indistinguishable from a genuinely broken one
				// if checked even slightly too early. Simpler and unambiguous.
				Btn->SetVisibility(ESlateVisibility::Visible);
				ChoiceButtons.Add(Btn);
			}
			else
			{
				// Content already cleared above (Btn->SetContent(nullptr)) —
				// nothing left to hide.
				Btn->SetVisibility(ESlateVisibility::Collapsed);
			}
		}


		// Default selection on first available
		SelectedIndex = 0;
		for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
			if (ChoiceButtons[i]->GetIsEnabled()) { SelectedIndex = i; break; }
		bKeyboardSelectionActive = false;
		HighlightChoice(SelectedIndex);
		return;
	}

	// ── Path B: dynamic construction (the only path — see above) ──
	// Every widget here is built fresh and added to a cleared ChoicesBox on
	// each call, so nothing survives between turns to render stale content.
	// All widgets are constructed with NAME_None rather than "ChoiceBtn_%d"/
	// "ChoiceRow_%d"/etc: the WBP already owns widgets under those exact
	// names, and reusing them risks a lookup/construction collision
	// resolving to the stale WBP widget instead of the new one.
	if (!ChoicesBox) return;
	ChoicesBox->ClearChildren();
	ChoiceButtons.Reset();
	ChoiceBaseTints.Reset();
	ChoiceLabelWordCounts.Reset();
	ChoiceWordBoxes.Reset();
	ChoiceRowBackgrounds.Reset();
	ChoiceLabelWidgets.Reset();
	ChoicePrefixLabels.Reset();

	// Make sure the container itself is actually visible — an earlier turn
	// (or Path A's Collapse-on-unused logic) can leave it hidden.
	ChoicesBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	for (int32 i = 0; i < Choices.Num() && i < MaxSlots; ++i)
	{
		const FEclipseDialogueChoice& Choice = Choices[i];

		// Each row is: [num-circle] [text]
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(
			UButton::StaticClass(), NAME_None);

		// Button chrome stays fully transparent — the visible card (white
		// bg at rest, black on hover/select) is the wrapping Border below;
		// NativeTick's hover pass repaints its brush.
		FButtonStyle BtnStyle;
		BtnStyle.Normal   = RoundedBrush(FLinearColor::Transparent, FLinearColor::Transparent, 0.f, 0.f);
		BtnStyle.Hovered  = BtnStyle.Normal;
		BtnStyle.Pressed  = BtnStyle.Normal;
		BtnStyle.Disabled = BtnStyle.Normal;
		Btn->SetStyle(BtnStyle);
		// Failed skill-check choices stay CLICKABLE — see MakeChoice for the
		// Energy-cost handling. But stage-directive gates ([STAT: N] /
		// [ITEM_NAME]) genuinely block: disable when GateHint is set.
		Btn->SetIsEnabled(Choice.GateHint.IsEmpty());

		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(
			UHorizontalBox::StaticClass(), NAME_None);

		// ── Circle number node (24x24) ──
		UBorder* CircleBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), NAME_None);
		CircleBg->SetBrush(RoundedBrush(
			FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),  // bg
			FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),  // chalk outline
			1.f, 12.f));
		CircleBg->SetPadding(FMargin(0.f));
		CircleBg->SetHorizontalAlignment(HAlign_Center);
		CircleBg->SetVerticalAlignment(VAlign_Center);

		UTextBlock* CircleNum = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		CircleNum->SetText(FText::AsNumber(i + 1));
		CircleNum->SetFont(MakeBMSPA(11));
		CircleNum->SetColorAndOpacity(FSlateColor(Cream));
		CircleNum->SetJustification(ETextJustify::Center);
		CircleBg->SetContent(CircleNum);

		// Wrap the circle in a SizeBox to enforce 24×24 footprint.
		USizeBox* CircleSize = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), NAME_None);
		CircleSize->SetWidthOverride(24.f);
		CircleSize->SetHeightOverride(24.f);
		CircleSize->AddChild(CircleBg);
		// Plain-text rows: number-circle chrome retired (kept in the tree
		// for WBP-name compatibility, but never shown).
		CircleSize->SetVisibility(ESlateVisibility::Collapsed);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(CircleSize))
		{
			S->SetPadding(FMargin(0.f, 1.f, 10.f, 0.f));
			S->SetVerticalAlignment(VAlign_Top);
		}

		// ── Text label ──
		// Split in two so only the stat tag is tinted: a coloured prefix
		// block ("Aesthetics [3]") followed by the neutral body (": I like
		// the outfit."). See SplitChoiceLabel.
		FString PrefixStr, LabelStr;
		SplitChoiceLabel(Choice, PrefixStr, LabelStr);
		if (!Choice.GateHint.IsEmpty())
		{
			LabelStr += TEXT("  ") + Choice.GateHint.ToString();
		}

		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		Label->SetText(FText::FromString(LabelStr));
		// Exactly the body text's typeface AND size — pulled off the
		// WBP-bound BodyText, the same source StartBodyAnimation uses for
		// the NPC's lines, then forced to the shared dialogue text size.
		// Choices and narration read as one system this way instead of the
		// choices having their own (previously hardcoded 20pt Rodin) look.
		{
			FSlateFontInfo ChoiceFont = BodyText ? BodyText->GetFont() : MakeRodin(/*Size=*/18);
			ChoiceFont.Size = 18;
			Label->SetFont(ChoiceFont);
		}
		// Body stays neutral whenever a coloured prefix is carrying the
		// stat identity; without a prefix the body itself takes ChoiceTint
		// (which is just black, or the red hint for a blocked pick).
		// NativeTick's hover pass swaps both the card and the text to the
		// opposite scheme (black card / white text), so this is the at-rest
		// value only.
		const bool bHasPrefix = !PrefixStr.IsEmpty();
		const FLinearColor Tint = bHasPrefix ? FLinearColor::Black : ChoiceTint(Choice);
		ChoiceBaseTints.Add(Tint);
		ChoiceLabelWidgets.Add(Label);
		ChoiceLabelWordCounts.Add(ChoiceLabelWordCount(Choice));
		Label->SetColorAndOpacity(FSlateColor(Tint));
		Label->SetAutoWrapText(true);
		// Bound the wrap so a long choice still breaks onto more lines
		// rather than running off the panel — the card hugs the text up to
		// this width, then grows downward instead of sideways.
		{
			float MaxTextW = 340.f;
			const float BoxW = ChoicesBox->GetCachedGeometry().GetLocalSize().X;
			if (BoxW > 120.f) MaxTextW = BoxW - 60.f;
			// The prefix shares the first line, so hand the body less room —
			// rough char-width estimate is enough to stop it overflowing the
			// card; exact metrics would need a laid-out geometry we don't
			// have yet at construction time.
			if (bHasPrefix) MaxTextW = FMath::Max(120.f, MaxTextW - PrefixStr.Len() * 8.f);
			Label->SetWrapTextAt(MaxTextW);
		}

		UTextBlock* PrefixLabel = nullptr;
		if (bHasPrefix)
		{
			PrefixLabel = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), NAME_None);
			PrefixLabel->SetText(FText::FromString(PrefixStr));
			PrefixLabel->SetFont(Label->GetFont());
			PrefixLabel->SetColorAndOpacity(FSlateColor(ChoiceTint(Choice)));
			// Never wraps — the tag is short and should stay on one line so
			// the body wraps beneath it as a hanging indent.
			PrefixLabel->SetAutoWrapText(false);
			if (UHorizontalBoxSlot* PS = Row->AddChildToHorizontalBox(PrefixLabel))
			{
				PS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
				PS->SetVerticalAlignment(VAlign_Center);
			}
		}
		ChoicePrefixLabels.Add(PrefixLabel);

		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Label))
		{
			// Auto (not Fill) so the row is only as wide as the text —
			// Fill would stretch it to the full column width.
			S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
			S->SetVerticalAlignment(VAlign_Center);
		}

		// Wrap the row in a white card — NativeTick flips this brush to
		// black on hover/select, mirroring the row's text colour swap.
		UBorder* RowBg = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), NAME_None);
		RowBg->SetBrush(RoundedBrush(FLinearColor::White, FLinearColor::Transparent, 0.f, 8.f));
		RowBg->SetPadding(FMargin(10.f, 6.f));
		RowBg->SetContent(Row);
		ChoiceRowBackgrounds.Add(RowBg);

		Btn->SetContent(RowBg);
		// Left-aligned, NOT Fill — the card shrink-wraps to its text so a
		// short choice reads as a short chip rather than a full-width bar.
		if (UButtonSlot* BS = Cast<UButtonSlot>(RowBg->Slot))
		{
			BS->SetHorizontalAlignment(HAlign_Left);
			BS->SetVerticalAlignment(VAlign_Center);
		}
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
			// Left, not the UVerticalBoxSlot default of Fill — otherwise the
			// button (and so its click area) would still span the whole
			// column even though the card inside it hugs the text.
			S->SetHorizontalAlignment(HAlign_Left);
		}

		// Held back until the NPC's line has finished printing — see the
		// reveal pass in NativeTick. Hidden via RenderOpacity rather than
		// Collapsed so the row keeps its layout space and nothing jumps
		// when it appears.
		Btn->SetRenderOpacity(0.f);
		ChoiceRevealButtons.Add(Btn);

		ChoiceButtons.Add(Btn);
	}

	// Default selection to the first available choice
	SelectedIndex = 0;
	for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
	{
		if (ChoiceButtons[i]->GetIsEnabled()) { SelectedIndex = i; break; }
	}
	bKeyboardSelectionActive = false;
	HighlightChoice(SelectedIndex);
}

void UEclipseDialogueWidget::HighlightChoice(int32 Index)
{
	// Ghost-button restyle: selection is shown by the TEXT going white
	// (NativeTick hover/selection pass reads SelectedIndex), never by a
	// background brush. Just record the selection.
	SelectedIndex = Index;
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
			bKeyboardSelectionActive = true;
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
	if (K == EKeys::E || K == EKeys::Enter || K == EKeys::SpaceBar)
	{
		// While anything is still printing, E means "hurry up", not "pick".
		// Choices aren't even visible yet at that point, so treating it as a
		// selection would act on options the player can't see.
		if (bDialogueAnimating || bPlayerLineAnimating || PendingNode.IsSet() || bChoiceCommitted)
		{
			SkipToChoices();
		}
		else
		{
			MakeChoice(SelectedIndex);
		}
		return FReply::Handled();
	}
	if (K == EKeys::Escape)                      { OnCloseClicked(); return FReply::Handled(); }
	// Number keys 1-5 jump-select
	for (int32 i = 0; i < ChoiceButtons.Num() && i < 5; ++i)
	{
		const FKey NumKey = (i == 0 ? EKeys::One : i == 1 ? EKeys::Two :
		                     i == 2 ? EKeys::Three : i == 3 ? EKeys::Four : EKeys::Five);
		if (K == NumKey && ChoiceButtons[i]->GetIsEnabled())
		{
			SelectedIndex = i;
			bKeyboardSelectionActive = true;
			HighlightChoice(SelectedIndex);
			MakeChoice(i);
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UEclipseDialogueWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// The scrollbox never consumes wheel input itself (SetConsumeMouseWheel
	// Never) — every tick over any part of the panel lands here, so this is
	// the single, uniform scroll path: one speed, one easing curve, one
	// pixel-snap, everywhere over the box.
	if (RightHistoryScroll)
	{
		const float ScrollStep = 14.f;   // small step — deliberately slow
		const float Target = bDialogueScrollTargetActive ? DialogueScrollTargetOffset : RightHistoryScroll->GetScrollOffset();
		DialogueScrollTargetOffset = FMath::Clamp(
			Target - InMouseEvent.GetWheelDelta() * ScrollStep,
			0.f, RightHistoryScroll->GetScrollOffsetOfEnd());
		bDialogueScrollTargetActive = true;
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
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

namespace { float WordHoldDuration(const FString& Word); FLinearColor Transparent(FLinearColor C); }

void UEclipseDialogueWidget::SkipToChoices()
{
	// Reveal everything still mid-cascade, in both the NPC's line and the
	// player's, then release the pending-node beat so the next node lands
	// this frame instead of after its delay. NativeTick's own reveal loops
	// then find nothing left to do and the choice-reveal gate opens.
	auto RevealAll = [](TArray<TObjectPtr<UWidget>>& Blocks)
	{
		for (const TObjectPtr<UWidget>& Block : Blocks)
		{
			if (!Block) continue;
			if (UTextBlock* T = Cast<UTextBlock>(Block))
			{
				FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
				if (C.A < 1.f) { C.A = 1.f; T->SetColorAndOpacity(FSlateColor(C)); }
			}
			else if (Block->GetVisibility() == ESlateVisibility::Collapsed)
			{
				Block->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			}
		}
	};

	RevealAll(AnimWordBlocks);
	RevealAll(PlayerAnimWordBlocks);

	bDialogueAnimating   = false;
	bPlayerLineAnimating = false;
	DialogueAnimTime     = BodyAnimTotalTime;

	// Cut the mumble voice — the line it was narrating is already fully on
	// screen, so letting it ring on would be a stray sound with no source.
	for (TWeakObjectPtr<UAudioComponent>& Weak : ActiveMumbleSlices)
	{
		if (UAudioComponent* Live = Weak.Get()) Live->FadeOut(0.06f, 0.f);
	}
	ActiveMumbleSlices.Reset();

	// Land the NPC's turn now rather than after PendingNodeDelay.
	if (PendingNode.IsSet())
	{
		FEclipseDialogueNodeView Node = PendingNode.GetValue();
		PendingNode.Reset();
		ApplyNodeChanged(Node);
		return;   // ApplyNodeChanged starts a fresh cascade; leave it running.
	}

	SetBodyPrintingFlag(false);
	if (RightHistoryScroll) RightHistoryScroll->ScrollToEnd();
}

void UEclipseDialogueWidget::MakeChoice(int32 Index)
{
	using namespace EclipseUI;

	// Second press on an already-committed node = quick skip, not a second
	// choice. This is the fix for "mash E and the same option fires twice":
	// the choice is dispatched asynchronously, so CurrentChoices still holds
	// the OLD options for a few frames afterwards and a second press would
	// happily pick from them again.
	if (bChoiceCommitted)
	{
		SkipToChoices();
		return;
	}
	bChoiceCommitted = true;

	// Hold the HUD's side-quest checklist from here rather than from the
	// start of the next body cascade. Ink runs the whole branch — including
	// its `~ SideQuests += ...` — the instant the choice is dispatched, so
	// the gap between "clicked" and "NPC starts talking" was exactly when a
	// new quest used to pop on screen early.
	SetBodyPrintingFlag(true);

	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueChoiceSound);
	}

	// [CONTINUE] isn't a real player turn — same speaker, just the next
	// paragraph — so skip the hide/re-show flicker on the portrait + name
	// tag below, and skip stamping a "YOU" bubble further down.
	const bool bIsContinuePrompt = CurrentChoices.IsValidIndex(Index) && CurrentChoices[Index].bIsContinuePrompt;

	// The player's turn starts now — hide the NPC's top-left name tag +
	// portrait. HandleNodeChanged re-shows them the moment the NPC speaks
	// again (or they stay hidden if this choice closes the dialogue).
	if (!bIsContinuePrompt)
	{
		if (SpeakerNameText) SpeakerNameText->SetVisibility(ESlateVisibility::Collapsed);
		if (SpeakerPortrait) SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);
	}

	// Collapse (not remove) the just-clicked options — ChoicesBox stays
	// exactly where it is in RightHistoryScroll (right after the NPC's last
	// line), just zero-height now, so the player's row appended below lands
	// in that same spot and the transcript reads as one continuous flow.
	// HandleNodeChanged re-parents + re-shows ChoicesBox at the new end of
	// the scroll once the next node's choices are ready.
	if (ChoicesBox) ChoicesBox->SetVisibility(ESlateVisibility::Collapsed);

	// Stamp a player-side caption row with the chosen line BEFORE dispatching
	// to the subsystem — once MakeChoice returns the dialogue has already
	// advanced and CurrentChoices points at the next node's options. Player
	// rows share the same RightHistoryScroll as the NPC's, same per-sentence
	// black-box grouping — but every sentence box (and every word inside it)
	// pops in AT ONCE, no stagger: the player already committed to this
	// line, so it reads as something just said, not something being spoken.
	// Skipped for the synthetic [CONTINUE] prompt — it isn't something the
	// player said, just pacing between the NPC's own paragraphs.
	if (RightHistoryScroll && CurrentChoices.IsValidIndex(Index) && !bIsContinuePrompt)
	{
		const FEclipseDialogueChoice& Picked = CurrentChoices[Index];
		UWrapBox* Words = AppendBubble(
			RightHistoryScroll,
			NSLOCTEXT("Eclipse", "PlayerSpeakerCaption", "YOU"),
			Cream,                          // player caption tint
			/*EffectsOut=*/nullptr,
			/*bAlignRight=*/true);
		if (Words && WidgetTree)
		{
			// The line WITHOUT its check label — see PlainChoiceText. The
			// choice button already showed "Rhythm [1]:"; repeating it in the
			// transcript reads as the player having said it out loud.
			TArray<TArray<FString>> Sentences = SplitIntoSentences(PlainChoiceText(Picked));
			// Pull the designer's font FAMILY from BodyText (same source
			// StartBodyAnimation uses for the NPC's lines) so "YOU" reads in
			// the same typeface as the speaker, just forced to the shared
			// dialogue text size.
			FSlateFontInfo LineFont = BodyText ? BodyText->GetFont() : MakeRodin(/*Size=*/18);
			LineFont.Size = 18;

			float WrapW = 400.f;
			{
				const float LocalW = Words->GetCachedGeometry().GetLocalSize().X;
				if (LocalW > 120.f) WrapW = LocalW - 24.f;
			}

			// Same word-by-word cascade as the NPC's lines (StartBodyAnimation)
			// — its own independent timeline (PlayerAnimWordBlocks/Delays) so
			// it keeps revealing even after DS->MakeChoice below advances to
			// the next node and resets the NPC-side AnimWordBlocks for itself.
			PlayerAnimWordBlocks.Reset();
			PlayerAnimWordDelays.Reset();
			float RunningDelay = 0.f;
			int32 SentenceIndex = 0;
			for (const TArray<FString>& Sentence : Sentences)
			{
				FSentenceBox SBox = BeginSentenceBox(Words, WrapW, SentenceIndex,
					SentenceIndex == 0, SentenceIndex == Sentences.Num() - 1, /*bAlignRight=*/true);
				++SentenceIndex;
				if (!SBox.Box || !SBox.Inner) continue;

				bool bFirstWordInSentence = true;
				for (const FString& W : Sentence)
				{
					if (bFirstWordInSentence)
					{
						PlayerAnimWordBlocks.Add(SBox.Box);
						PlayerAnimWordDelays.Add(RunningDelay);
						bFirstWordInSentence = false;
					}

					// Stays laid out (SelfHitTestInvisible, not Collapsed) from
					// the moment it's created — this row is right-aligned, so
					// a Collapsed→Visible reveal would reflow the whole line
					// leftward on every new word, shoving already-revealed
					// words sideways instead of reading as a steady left-to-
					// right cascade. Transparent-to-opaque keeps every word's
					// position fixed; only its alpha animates.
					UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
						UTextBlock::StaticClass(), NAME_None);
					Text->SetFont(LineFont);
					Text->SetColorAndOpacity(FSlateColor(Transparent(FLinearColor::White)));
					Text->SetText(FText::FromString(W + TEXT(" ")));
					Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
					SBox.Inner->AddChildToWrapBox(Text);

					PlayerAnimWordBlocks.Add(Text);
					PlayerAnimWordDelays.Add(RunningDelay);

					RunningDelay += WordHoldDuration(W);
				}
			}
			PlayerAnimTime = 0.f;
			bPlayerLineAnimating = PlayerAnimWordBlocks.Num() > 0;
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
//  Word-by-word caption reveal
//
//  Splits the body string on whitespace and constructs one black caption
//  chip per word (BuildWordChip) inside BodyWords (a UWrapBox). Each chip
//  starts Collapsed and pops straight to Visible — no fade — when its
//  scheduled delay is reached. Words are staggered by WordHoldDuration
//  below — a closed-caption cadence, not a metronomic text-message tick:
//  the pause before each word scales with the word that came before it, so
//  the cascade breathes instead of ticking at a constant rate.
// ─────────────────────────────────────────────────────────────

namespace
{
	// Closed-caption-style pacing. The hold time returned here is how long
	// THIS word "occupies the mic" before the next one is allowed to
	// appear — long words hold the beat longer, short words snap by
	// quickly, and a trailing clause mark (. , ! ? ; :) adds a small extra
	// beat for the pause a reader naturally takes there. Clamped so one
	// very long word can't stall the whole line.
	float WordHoldDuration(const FString& Word)
	{
		constexpr float BaseHold   = 0.075f;  // floor — short words ("a", "is")
		constexpr float PerChar    = 0.014f;  // extra seconds per character
		constexpr float MaxHold    = 0.30f;   // ceiling before clause bonus
		constexpr float PunctBonus = 0.10f;   // clause-boundary pause

		float Hold = BaseHold + PerChar * static_cast<float>(Word.Len());
		Hold = FMath::Min(Hold, MaxHold);

		if (Word.Len() > 0)
		{
			const TCHAR Last = Word[Word.Len() - 1];
			if (Last == TEXT('.') || Last == TEXT(',') || Last == TEXT('!') ||
				Last == TEXT('?') || Last == TEXT(';') || Last == TEXT(':'))
			{
				Hold += PunctBonus;
			}
		}
		return Hold;
	}

	// Same color, alpha zeroed — used to seed a word's starting color for the
	// alpha-reveal cascade (StartBodyAnimation, AnimateChoiceText, MakeChoice).
	FLinearColor Transparent(FLinearColor C)
	{
		C.A = 0.f;
		return C;
	}

	// Colors "<Stat> Damaged"/"<Stat> Improved" bigrams — as authored in body
	// text, e.g. "Aesthetics Damaged: Level 3" — with that stat's hue
	// (EclipseUI::StatHue, same palette as the skill-check choice tints), so
	// stat-altering narration reads distinctly from plain prose. Every other
	// word stays white.
	TArray<FLinearColor> ColorStatWords(const TArray<FString>& Sentence)
	{
		TArray<FLinearColor> Colors;
		Colors.Init(FLinearColor::White, Sentence.Num());
		for (int32 i = 0; i + 1 < Sentence.Num(); ++i)
		{
			FString Verb = Sentence[i + 1];
			Verb.RemoveFromEnd(TEXT(":"));
			if (!Verb.Equals(TEXT("Damaged"), ESearchCase::IgnoreCase) &&
				!Verb.Equals(TEXT("Improved"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			FString Stat = Sentence[i];
			Stat.RemoveFromEnd(TEXT(":"));
			if (TOptional<FLinearColor> Hue = EclipseUI::StatHue(FName(*Stat)))
			{
				Colors[i] = Colors[i + 1] = *Hue;
			}
		}
		return Colors;
	}
}

void UEclipseDialogueWidget::SetBodyPrintingFlag(bool bPrinting)
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			DS->SetBodyPrinting(bPrinting);
		}
	}
}

void UEclipseDialogueWidget::StartBodyAnimation(const FString& BodyString)
{
	using namespace EclipseUI;

	if (!BodyWords || !WidgetTree) return;

	// Wipe previous run — also wipes any choice-row words from the prior node.
	BodyWords->ClearChildren();
	AnimWordBlocks.Reset();
	AnimWordDelays.Reset();
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

	// Group into sentences — one closed-caption black box per sentence,
	// not per word (BeginSentenceBox / SplitIntoSentences).
	TArray<TArray<FString>> Sentences = SplitIntoSentences(BodyString);

	// Pick up the designer's font FAMILY from the WBP-bound BodyText so
	// word blocks inherit whatever typeface the designer set (e.g.
	// Pantasia) — but force the SIZE to the shared dialogue text size,
	// bumped up so captions read like subtitles printed on screen.
	FSlateFontInfo BodyFont = BodyText
		? BodyText->GetFont()
		: MakeRodin(/*Size=*/18);
	BodyFont.Size = 18;

	// Wrap width for each sentence box's inner text — matches the width
	// AppendBubble gave the outer WrapBox (BodyWords). Cached geometry can
	// still be zero on the very first frame after opening; fall back safe.
	float WrapW = 400.f;
	{
		const float LocalW = BodyWords->GetCachedGeometry().GetLocalSize().X;
		if (LocalW > 120.f) WrapW = LocalW - 24.f;
	}

	// Cumulative closed-caption pacing — see WordHoldDuration. RunningDelay
	// tracks when the NEXT word is allowed to start; each word's own delay
	// is recorded before its hold time is added to the running total. The
	// sentence's box shares the FIRST word's delay (Collapsed until then),
	// so it pops in exactly as its first word does and grows in place as
	// the rest of the sentence reveals.
	float RunningDelay = 0.f;
	int32 SentenceIndex = 0;
	for (const TArray<FString>& Sentence : Sentences)
	{
		FSentenceBox SBox = BeginSentenceBox(BodyWords, WrapW, SentenceIndex,
			SentenceIndex == 0, SentenceIndex == Sentences.Num() - 1);
		++SentenceIndex;
		if (!SBox.Box || !SBox.Inner) continue;

		const TArray<FLinearColor> WordColors = ColorStatWords(Sentence);
		bool bFirstWordInSentence = true;
		for (int32 WordIdx = 0; WordIdx < Sentence.Num(); ++WordIdx)
		{
			const FString& Word = Sentence[WordIdx];
			if (bFirstWordInSentence)
			{
				// The box itself is a reveal target too. Marked "already
				// fired" so it never triggers its own mumble slice (only
				// real words do).
				//
				// It gets a short HEAD START over its own first word rather
				// than sharing that word's delay. The box reveals via
				// Collapsed→Visible, and Slate doesn't lay out collapsed
				// widgets — so on the frame the box appears, its contents can
				// paint once against stale (zero) geometry before the
				// re-arrange lands. That one frame is the "first word of the
				// line flashes over on the right, then drops into the box"
				// glitch. Letting the box get arranged first removes it.
				// (Same class of bug as the trailing-word reflow the
				// alpha-reveal below fixed, different trigger.)
				AnimWordBlocks.Add(SBox.Box);
				AnimWordDelays.Add(RunningDelay);
				AnimWordMumbleFired.Add(true);
				RunningDelay += SentenceBoxLayoutLead;
				bFirstWordInSentence = false;
			}

			UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), NAME_None);
			Text->SetFont(BodyFont);
			// Stays laid out (SelfHitTestInvisible, not Collapsed) from the
			// moment it's created — same reasoning as the player's "YOU" line
			// (see MakeChoice): a Collapsed→Visible reveal makes the
			// containing WrapBox redo its line-wrap decision every time a
			// word pops in, which can render the word on the current line
			// for a frame before it jumps down to its correct wrapped line.
			// Transparent-to-opaque keeps every word's line fixed from the
			// start; only its alpha animates.
			Text->SetColorAndOpacity(FSlateColor(Transparent(WordColors[WordIdx])));
			// Trailing space separates words sharing the same box.
			Text->SetText(FText::FromString(Word + TEXT(" ")));
			Text->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			SBox.Inner->AddChildToWrapBox(Text);

			AnimWordBlocks.Add(Text);
			AnimWordDelays.Add(RunningDelay);
			AnimWordMumbleFired.Add(false);

			RunningDelay += WordHoldDuration(Word);
		}
	}

	// Total time the body takes to fully resolve = last word's reveal time
	// + a short breathing pause before the choice rows start cascading in.
	constexpr float PostBodyPause = 0.15f;
	BodyAnimTotalTime = (AnimWordDelays.Num() > 0)
		? (AnimWordDelays.Last() + PostBodyPause)
		: 0.f;

	DialogueAnimTime  = 0.f;
	bDialogueAnimating = AnimWordBlocks.Num() > 0;
	SetBodyPrintingFlag(bDialogueAnimating);

	UE_LOG(LogEclipse, Log, TEXT("Dlg: StartBodyAnimation — %d words, total=%.2fs, anim=%s"),
		AnimWordBlocks.Num(), BodyAnimTotalTime, bDialogueAnimating ? TEXT("ON") : TEXT("OFF"));
}

void UEclipseDialogueWidget::AnimateChoiceText(UTextBlock* Label, int32 ChoiceIndex,
	const FString& Text, const FLinearColor& TargetTint, float StartDelay,
	int32 LabelWordCount, const FLinearColor& LabelTint)
{
	using namespace EclipseUI;
	if (!Label || !WidgetTree) return;

	UPanelWidget* Parent = Label->GetParent();
	if (!Parent) return;

	// Hide the static label — the wrap box of words will replace its visual role.
	Label->SetText(FText::GetEmpty());
	Label->SetVisibility(ESlateVisibility::Collapsed);

	// Always build a fresh wrap box rather than reusing/clearing the
	// previous one. Diagnostic logging proved the widget tree and word data
	// were already correct on every call — right words, right slot, right
	// reveal timing — even when the on-screen row still showed stale (or
	// no) text. So the bug wasn't logic, it was stale Slate render/paint
	// state tied to reusing the same WrapBox instance across a big content
	// change (e.g. the 1-word "[CONTINUE]" prompt replaced by an 8-word
	// real choice). Discarding and reconstructing sidesteps whatever cached
	// geometry the old instance was carrying — 5 slots max, the extra
	// allocation is free.
	if (!ChoiceWordBoxes.IsValidIndex(ChoiceIndex))
	{
		ChoiceWordBoxes.SetNum(ChoiceIndex + 1);
	}
	if (UWrapBox* Old = ChoiceWordBoxes[ChoiceIndex])
	{
		Old->RemoveFromParent();
	}
	UWrapBox* WB = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), NAME_None);
	ChoiceWordBoxes[ChoiceIndex] = WB;
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
	WB->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	TArray<FString> Words;
	Text.ParseIntoArrayWS(Words);

	// Per-word blocks inherit the designer's font FAMILY from the WBP-bound
	// choice label (e.g. ChoiceText_0..4), but the SIZE is forced to match
	// the (now much bigger, caption-style) body text.
	FSlateFontInfo ChoiceFont = Label
		? Label->GetFont()
		: MakeRodin(/*Size=*/18);
	ChoiceFont.Size = 18;

	// Same closed-caption cadence as the body cascade (see WordHoldDuration),
	// offset by StartDelay so the row still waits its turn in the stagger.
	// No chip/box here — choice rows stay plain text (see ChoiceTint) — laid
	// out (SelfHitTestInvisible, alpha 0) from construction and revealed via
	// alpha, same convention as the body cascade (see StartBodyAnimation) —
	// the shared per-word reveal loop in NativeTick only knows how to fade
	// TextBlocks in via alpha, not pop them from Collapsed.
	float RunningDelay = StartDelay;
	for (int32 wi = 0; wi < Words.Num(); ++wi)
	{
		// NAME_None (not an explicit "ChoiceWord_%d_%d") — matches the body
		// words' construction (StartBodyAnimation). An explicit, reused name
		// here meant that when a choice's word COUNT changed between calls
		// (e.g. the 1-word "[CONTINUE]" prompt replaced by a 4-word real
		// choice), reconstructing under the same name for index 0 could
		// return/rename around the stale old widget instead of a clean new
		// one, leaving old text visible. WB->ClearChildren() below still
		// discards the actual old widgets from the tree either way.
		UTextBlock* W = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), NAME_None);
		W->SetText(FText::FromString(Words[wi] + TEXT(" ")));
		W->SetFont(ChoiceFont);
		const FLinearColor WordTint = (wi < LabelWordCount) ? LabelTint : TargetTint;
		W->SetColorAndOpacity(FSlateColor(Transparent(WordTint)));
		W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WB->AddChildToWrapBox(W);

		AnimWordBlocks.Add(W);
		AnimWordDelays.Add(RunningDelay);
		AnimWordMumbleFired.Add(false);

		RunningDelay += WordHoldDuration(Words[wi]);
	}

	bDialogueAnimating = true;
}

void UEclipseDialogueWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);

	// ── Wheel-scroll easing ───────────────────────────────────────────────
	// The scrollbox never handles wheel itself (see SetConsumeMouseWheel),
	// so this is the only place scroll position actually moves. Interp
	// toward the target each frame — and ROUND to a whole pixel before
	// applying, since a fractional scroll offset sub-pixel-shifts every
	// text glyph in the transcript and reads as blurry while moving.
	if (bDialogueScrollTargetActive && RightHistoryScroll)
	{
		const float Current = RightHistoryScroll->GetScrollOffset();
		const float Eased = FMath::FInterpTo(Current, DialogueScrollTargetOffset, DeltaSeconds, 8.f);
		const float NewOffset = FMath::RoundToFloat(Eased);
		RightHistoryScroll->SetScrollOffset(NewOffset);
		if (FMath::IsNearlyEqual(NewOffset, DialogueScrollTargetOffset, 1.f))
		{
			RightHistoryScroll->SetScrollOffset(FMath::RoundToFloat(DialogueScrollTargetOffset));
			bDialogueScrollTargetActive = false;
		}
	}

	// ── Choice hover/selection recolour ──────────────────────────────────
	// White card / black text at rest; hovered or keyboard-selected rows
	// flip to a black card / white text (mirrors the NPC caption boxes'
	// own black-card look). The hot text also pulses subtly — a slow
	// brightness breathe, not a blink — as a hover affordance. Skipped
	// while the reveal animation owns the word colours.
	ChoicePulseTime += DeltaSeconds;
	if (!bDialogueAnimating && WidgetTree)
	{
		for (int32 i = 0; i < ChoiceButtons.Num(); ++i)
		{
			UButton* B = ChoiceButtons[i];
			if (!B) continue;
			const FLinearColor Base = ChoiceBaseTints.IsValidIndex(i)
				? ChoiceBaseTints[i] : FLinearColor::Black;
			const bool bHot = B->IsHovered() || (bKeyboardSelectionActive && i == SelectedIndex);

			FLinearColor C = Base;
			if (bHot)
			{
				// White at rest, breathing toward a pink-red accent and back
				// — a pulse, not a blink, and never fully saturating to red.
				const float Blend = 0.5f + 0.5f * FMath::Sin(ChoicePulseTime * 3.2f);
				C = FMath::Lerp(FLinearColor::White, EclipseUI::DialogueRed, Blend * 0.5f);
			}

			if (ChoiceRowBackgrounds.IsValidIndex(i) && ChoiceRowBackgrounds[i])
			{
				// Hot state matches the NPC caption boxes' own card exactly —
				// same black, same 80% alpha, not fully opaque.
				const FLinearColor CardColor = bHot
					? FLinearColor(0.f, 0.f, 0.f, 0.80f)
					: FLinearColor::White;
				ChoiceRowBackgrounds[i]->SetBrush(EclipseUI::RoundedBrush(CardColor, FLinearColor::Transparent, 0.f, 8.f));
			}

			// Path A tracks its label directly (ChoiceLabelWidgets — see
			// RebuildChoices for why this isn't a by-name FindWidget lookup
			// anymore). Path B still names its dynamically-built label
			// "ChoiceText_%d", so fall back to that if there's no tracked
			// reference for this slot.
			UTextBlock* L = ChoiceLabelWidgets.IsValidIndex(i) ? ChoiceLabelWidgets[i].Get() : nullptr;
			if (!L)
			{
				const FName LabelName(*FString::Printf(TEXT("ChoiceText_%d"), i));
				L = Cast<UTextBlock>(WidgetTree->FindWidget(LabelName));
			}
			if (L)
			{
				L->SetColorAndOpacity(FSlateColor(C));
			}

			// The stat tag keeps its hue in BOTH states — it's an identity
			// marker, not a hover affordance — but swaps palette with the
			// card underneath it: the deep variant reads on the white
			// resting card, the pastel variant on the black hovered one.
			// Using one palette for both would leave it near-invisible in
			// one of the two states.
			if (UTextBlock* P = ChoicePrefixLabels.IsValidIndex(i) ? ChoicePrefixLabels[i].Get() : nullptr)
			{
				const FEclipseDialogueChoice* Ch = CurrentChoices.IsValidIndex(i) ? &CurrentChoices[i] : nullptr;
				const FName Stat = Ch
					? (Ch->bIsSkillCheck ? Ch->SkillCheckStat : Ch->StatCheckLabelStat)
					: NAME_None;
				const TOptional<FLinearColor> Hue = bHot
					? EclipseUI::StatHue(Stat)
					: EclipseUI::StatHueDeep(Stat);
				P->SetColorAndOpacity(FSlateColor(Hue.Get(C)));
			}
			// Word-cascade path — recolour every word block in the row,
			// except the stat-check label prefix (if any), which keeps its
			// own stat hue permanently rather than following hover/base.
			const int32 LabelWordCount = ChoiceLabelWordCounts.IsValidIndex(i) ? ChoiceLabelWordCounts[i] : 0;
			if (UWrapBox* WB = ChoiceWordBoxes.IsValidIndex(i) ? ChoiceWordBoxes[i].Get() : nullptr)
			{
				int32 WordIdx = 0;
				for (UWidget* Child : WB->GetAllChildren())
				{
					if (UTextBlock* T = Cast<UTextBlock>(Child))
					{
						if (WordIdx >= LabelWordCount)
						{
							T->SetColorAndOpacity(FSlateColor(C));
						}
						++WordIdx;
					}
				}
			}
		}
	}

	// ── Player "YOU" line reveal ──────────────────────────────────────────
	// Independent of bDialogueAnimating/DialogueAnimTime below — this has to
	// keep running even after the NPC's next line starts its own cascade.
	if (bPlayerLineAnimating)
	{
		PlayerAnimTime += DeltaSeconds;
		bool bPlayerAllDone = true;
		for (int32 i = 0; i < PlayerAnimWordBlocks.Num(); ++i)
		{
			UWidget* Block = PlayerAnimWordBlocks[i];
			if (!Block) continue;
			const float Delay = PlayerAnimWordDelays.IsValidIndex(i) ? PlayerAnimWordDelays[i] : 0.f;
			const bool bDue = PlayerAnimTime >= Delay;
			if (!bDue) bPlayerAllDone = false;

			// Words fade in via alpha (right-aligned row — see MakeChoice for
			// why a Collapsed reveal would reflow the line). The sentence
			// box itself still reveals via visibility — it's an atomic
			// single pop, not incremental, so reflow isn't a concern there.
			if (UTextBlock* T = Cast<UTextBlock>(Block))
			{
				if (bDue)
				{
					FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
					if (C.A < 1.f)
					{
						C.A = 1.f;
						T->SetColorAndOpacity(FSlateColor(C));
						if (RightHistoryScroll) { RightHistoryScroll->ScrollToEnd(); bDialogueScrollTargetActive = false; }
					}
				}
			}
			else if (bDue && Block->GetVisibility() == ESlateVisibility::Collapsed)
			{
				Block->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
				if (RightHistoryScroll) { RightHistoryScroll->ScrollToEnd(); bDialogueScrollTargetActive = false; }
			}
		}
		if (bPlayerAllDone) bPlayerLineAnimating = false;
	}

	// ── Beat before the NPC's turn ─────────────────────────────────────────
	// HandleNodeChanged held the NPC's turn back in PendingNode while the
	// player's line was revealing (started PendingNodeTimer at 0 the moment
	// it did). Once the player's line finishes (bPlayerLineAnimating clears
	// above), keep ticking that timer here — every frame, not just while the
	// player line block above is still running — so there's a small beat
	// before the NPC's name/portrait/line pop in, instead of it snapping in
	// the instant the player's line stops.
	if (PendingNode.IsSet() && !bPlayerLineAnimating)
	{
		PendingNodeTimer += DeltaSeconds;
		if (PendingNodeTimer >= PendingNodeDelay)
		{
			FEclipseDialogueNodeView Node = PendingNode.GetValue();
			PendingNode.Reset();
			ApplyNodeChanged(Node);
		}
	}

	// ── Choice reveal gate ────────────────────────────────────────────────
	// Choices stay invisible until the NPC's line has finished printing, so
	// the player can't answer a sentence they haven't read yet.
	//
	// Deliberately placed BEFORE the bDialogueAnimating early-out below and
	// evaluated from scratch every frame: an earlier version keyed the
	// reveal off a timer that only advanced while that flag was set, which
	// left rows stranded permanently invisible on any turn where the body
	// had nothing to animate. Here the gate is simply "the body isn't
	// animating any more", which is guaranteed to become true — including
	// immediately for a node with no body text at all.
	if (ChoiceRevealButtons.Num() > 0)
	{
		const bool bBodyDone = !bDialogueAnimating || DialogueAnimTime >= BodyAnimTotalTime;
		if (bBodyDone)
		{
			bool bRevealedAny = false;
			for (UButton* B : ChoiceRevealButtons)
			{
				if (!B || B->GetRenderOpacity() >= 1.f) continue;
				B->SetRenderOpacity(1.f);
				bRevealedAny = true;
			}
			// Rows appearing grows the scrollable content — keep the view
			// pinned to the newest row.
			if (bRevealedAny && RightHistoryScroll)
			{
				RightHistoryScroll->ScrollToEnd();
				bDialogueScrollTargetActive = false;
			}
		}
	}

	if (!bDialogueAnimating) return;

	DialogueAnimTime += DeltaSeconds;

	bool bAllDone = true;

	// ── Per-word reveal ────────────────────────────────────────────────────
	// Words are laid out (SelfHitTestInvisible, alpha 0) from construction —
	// same reasoning as the player's "YOU" line (see StartBodyAnimation): a
	// Collapsed→Visible reveal makes the containing WrapBox redo its
	// line-wrap decision on every word, which can render a word on the
	// current line for a frame before it jumps down to its correct wrapped
	// line. Alpha-only reveal keeps every word's line fixed from the start.
	// Each sentence's own box is still the one thing that pops via
	// Collapsed→Visible — that's an atomic single reveal (all its words
	// already exist inside it), so there's no incremental reflow to glitch.
	const int32 N = AnimWordBlocks.Num();
	for (int32 i = 0; i < N; ++i)
	{
		UWidget* Block = AnimWordBlocks[i];
		if (!Block) continue;

		const float Delay = (AnimWordDelays.IsValidIndex(i) ? AnimWordDelays[i] : 0.f);
		const bool bDue = DialogueAnimTime >= Delay;
		if (!bDue) { bAllDone = false; continue; }

		bool bJustRevealed = false;

		if (UTextBlock* T = Cast<UTextBlock>(Block))
		{
			FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
			if (C.A < 1.f)
			{
				C.A = 1.f;
				T->SetColorAndOpacity(FSlateColor(C));
				bJustRevealed = true;

				// Leading edge: the moment a word is revealed, splice off a
				// random mumble slice — but only every Nth word so the mumble
				// feels like phrases rather than chatter. AnimWordMumbleFired
				// keeps it strictly one-shot per word.
				if (AnimWordMumbleFired.IsValidIndex(i) && !AnimWordMumbleFired[i])
				{
					AnimWordMumbleFired[i] = true;
					const int32 Stride = FMath::Max(1, MumbleWordsPerSlice);
					if (i % Stride == 0)
					{
						PlayMumbleSlice();
					}
				}
			}
		}
		else if (Block->GetVisibility() == ESlateVisibility::Collapsed)
		{
			Block->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			bJustRevealed = true;
		}

		if (bJustRevealed && RightHistoryScroll)
		{
			RightHistoryScroll->ScrollToEnd();
			bDialogueScrollTargetActive = false;
		}
	}

	if (bAllDone)
	{
		bDialogueAnimating = false;
		SetBodyPrintingFlag(false);

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
