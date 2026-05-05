// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseDialogueWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseDialogueSubsystem.h"

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

		if (UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Panel))
		{
			// 480 wide, full-height, anchored to right edge
			Slot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 1.f));
			Slot->SetAlignment(FVector2D(1.f, 0.f));
			Slot->SetPosition(FVector2D(0.f, 0.f));
			Slot->SetSize(FVector2D(480.f, 0.f));
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

		if (UCanvasPanelSlot* Slot = Root->AddChildToCanvas(CloseButton))
		{
			Slot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
			Slot->SetAlignment(FVector2D(1.f, 0.f));
			Slot->SetPosition(FVector2D(-18.f, 14.f));
			Slot->SetSize(FVector2D(28.f, 28.f));
			Slot->SetZOrder(2);
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

	// Hidden until a dialogue opens
	SetVisibility(ESlateVisibility::Collapsed);

	if (CloseButton)
		CloseButton->OnClicked.AddDynamic(this, &UEclipseDialogueWidget::OnCloseClicked);

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
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
}

void UEclipseDialogueWidget::HandleNodeChanged(FEclipseDialogueNodeView Node)
{
	SetVisibility(ESlateVisibility::Visible);

	if (SpeakerNameText)
		SpeakerNameText->SetText(FText::FromName(Node.SpeakerName));

	if (BodyText)
		BodyText->SetText(Node.Body);

	RebuildChoices(Node.Choices);
}

void UEclipseDialogueWidget::HandleDialogueClosed()
{
	SetVisibility(ESlateVisibility::Collapsed);
	if (SpeakerNameText) SpeakerNameText->SetText(FText::GetEmpty());
	if (BodyText)        BodyText->SetText(FText::GetEmpty());
	if (SpeakerPortrait) SpeakerPortrait->SetVisibility(ESlateVisibility::Hidden);

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

	// Restore player input
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
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
		for (int32 i = 0; i < MaxSlots; ++i)
		{
			UButton*    Btn   = PreBtns[i];
			UTextBlock* Label = PreTexts[i];
			if (!Btn) continue;

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
				Btn->SetVisibility(ESlateVisibility::Visible);
				Btn->SetIsEnabled(Choice.bAvailable);
				if (Label)
				{
					FString S = Choice.Text.ToString();
					if (!Choice.bAvailable) S += TEXT("  (need more)");
					Label->SetText(FText::FromString(S));
					// Always (re)set the color so a previous unavailable run
					// doesn't leave the slot greyed out for the next dialogue.
					Label->SetColorAndOpacity(FSlateColor(Choice.bAvailable
						? Cream
						: FLinearColor(0.35f, 0.35f, 0.35f, 1.f)));
				}
				ChoiceButtons.Add(Btn);
			}
			else
			{
				Btn->SetVisibility(ESlateVisibility::Collapsed);
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
	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
		DS->MakeChoice(Index);
}

void UEclipseDialogueWidget::OnCloseClicked()
{
	if (UEclipseDialogueSubsystem* DS = GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
		DS->CloseDialogue();
}

// Slot callbacks
void UEclipseDialogueWidget::OnChoice0() { MakeChoice(0); }
void UEclipseDialogueWidget::OnChoice1() { MakeChoice(1); }
void UEclipseDialogueWidget::OnChoice2() { MakeChoice(2); }
void UEclipseDialogueWidget::OnChoice3() { MakeChoice(3); }
void UEclipseDialogueWidget::OnChoice4() { MakeChoice(4); }
