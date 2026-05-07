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
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"

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
	}
	Super::NativeDestruct();
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
	// SelfHitTestInvisible: the root canvas itself ignores hit-testing so only
	// the actual buttons/panel intercept input. Without this, the fullscreen
	// canvas would be a hit-test layer over the whole viewport, producing
	// off-centre hover/click registration on the buttons inside.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (SpeakerNameText)
		SpeakerNameText->SetText(FText::FromName(Node.SpeakerName));

	// Body text — preferred path is the word-by-word fade-in via BodyWords
	// (UWrapBox of per-word UTextBlocks). If the WBP doesn't have BodyWords
	// bound, fall back to setting BodyText directly (legacy single block).
	UE_LOG(LogEclipse, Log, TEXT("Dlg: HandleNodeChanged — BodyWords=%s BodyText=%s"),
		BodyWords ? TEXT("BOUND") : TEXT("null"),
		BodyText  ? TEXT("BOUND") : TEXT("null"));
	if (BodyWords)
	{
		// Hide the static BodyText (if it's still in the tree from older WBP
		// templates) so the animated wrap-box is the only thing rendering.
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

	RebuildChoices(Node.Choices);
}

void UEclipseDialogueWidget::HandleDialogueClosed()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (SpeakerNameText) SpeakerNameText->SetText(FText::GetEmpty());
	if (BodyText)        BodyText->SetText(FText::GetEmpty());
	if (SpeakerPortrait) SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);

	// Tear down the per-word animation state so we don't keep ticking dead
	// references on the next NativeTick.
	bDialogueAnimating = false;
	DialogueAnimTime = 0.f;
	if (BodyWords) BodyWords->ClearChildren();
	AnimWordBlocks.Reset();
	AnimWordDelays.Reset();
	AnimWordTints.Reset();
	ChoiceRevealButtons.Reset();
	ChoiceRevealDelays.Reset();

	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueCloseSound);
	}

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
				Btn->SetIsEnabled(Choice.bAvailable);

				FString S = Choice.Text.ToString();
				if (!Choice.bAvailable) S += TEXT("  (need more)");
				const FLinearColor Tint = Choice.bAvailable
					? Cream
					: FLinearColor(0.35f, 0.35f, 0.35f, 1.f);

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
		Btn->SetIsEnabled(Choice.bAvailable);

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
		if (!Choice.bAvailable)
			LabelStr += TEXT("  (need more)");
		Label->SetText(FText::FromString(LabelStr));
		Label->SetFont(MakeRodin(15));
		Label->SetColorAndOpacity(FSlateColor(Choice.bAvailable
			? Cream
			: FLinearColor(0.35f, 0.35f, 0.35f, 1.f)));
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
	if (UEclipseAudioSubsystem* Audio = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		Audio->PlayUI(DialogueChoiceSound);
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

	// ParseIntoArrayWS splits on any whitespace and discards empty entries.
	TArray<FString> Words;
	BodyString.ParseIntoArrayWS(Words);

	for (int32 i = 0; i < Words.Num(); ++i)
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("DlgWord_%d"), i)));

		// Trailing space is part of the word so the wrap-box can break naturally.
		Block->SetText(FText::FromString(Words[i] + TEXT(" ")));
		Block->SetFont(MakeRodin(/*Size=*/18));

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

	for (int32 wi = 0; wi < Words.Num(); ++wi)
	{
		UTextBlock* W = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("ChoiceWord_%d_%d"), ChoiceIndex, wi)));
		W->SetText(FText::FromString(Words[wi] + TEXT(" ")));
		W->SetFont(MakeRodin(/*Size=*/15));

		FLinearColor Start = TargetTint; Start.A = 0.f;
		W->SetColorAndOpacity(FSlateColor(Start));
		WB->AddChildToWrapBox(W);

		AnimWordBlocks.Add(W);
		AnimWordDelays.Add(StartDelay + static_cast<float>(wi) * WordSpawnInterval);
		AnimWordTints.Add(TargetTint);
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

	// ── Per-word fade ─────────────────────────────────────────────────────
	const int32 N = AnimWordBlocks.Num();
	for (int32 i = 0; i < N; ++i)
	{
		UTextBlock* Block = AnimWordBlocks[i];
		if (!Block) continue;

		const float Delay  = (AnimWordDelays.IsValidIndex(i) ? AnimWordDelays[i] : 0.f);
		const FLinearColor Tint = (AnimWordTints.IsValidIndex(i) ? AnimWordTints[i] : EclipseUI::Cream);
		const float t = (DialogueAnimTime - Delay) / FMath::Max(0.0001f, WordFadeDuration);
		const float Alpha = FMath::Clamp(t, 0.f, 1.f);

		FLinearColor C = Tint; C.A = Alpha;
		Block->SetColorAndOpacity(FSlateColor(C));

		if (Alpha < 1.f) bAllDone = false;
	}

	if (bAllDone)
	{
		bDialogueAnimating = false;
	}
}
