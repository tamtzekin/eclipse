// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipsePhoneWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Phone widget — left-edge panel, mirrors the prototype's #phone-ui block.
//
//  Layout (top to bottom inside the outer panel):
//      [ PHONE                                              × ]   ← header
//      [ 17:03                                       ₡ 0  $ 0 ]   ← status row
//      [ CONTACTS │ NOTES                                     ]   ← tabs
//      [ ─────────────────────────────────────────────────── ]
//      [                                                     ]
//      [   placeholder content — fills the scroll area       ]   ← content
//      [                                                     ]
//      [ ─────────────────────────────────────────────────── ]
//      [ CALL                                           TEXT ]   ← actions
//
//  Width: 280 px (slightly wider than prototype's 260 for readability).
//  Anchored left edge, vertically centred. Pauses the world while open
//  (same pattern as Inventory). ESC + P both close.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipsePhoneWidget::Initialize()
{
	using namespace EclipseUI;

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("ClockText"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		// ── Outer phone panel — navy gradient + cyan trim ────────────────
		UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(
			UBorder::StaticClass(), TEXT("PhonePanel"));
		Panel->SetBrush(RoundedBrush(
			FLinearColor(0.039f, 0.043f, 0.078f, 0.97f),  // deep navy fill
			FLinearColor(0.318f, 0.933f, 0.988f, 0.55f),  // cyan-ish trim
			1.5f, 10.f));
		Panel->SetPadding(FMargin(16.f, 14.f));

		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(Panel))
		{
			// Left-edge, vertically centred, 280×460. Y-alignment via 0.5
			// anchor pair so the panel hangs centred regardless of viewport
			// height.
			CSlot->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));
			CSlot->SetAlignment(FVector2D(0.f, 0.5f));
			CSlot->SetPosition(FVector2D(24.f, 0.f));
			CSlot->SetSize(FVector2D(280.f, 460.f));
		}

		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("PhoneColumn"));
		Panel->SetContent(Column);

		// ── 1. Header strip: "PHONE" + close × ──────────────────────────
		{
			UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("PhoneHeaderRow"));

			UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("PhoneTitle"));
			Title->SetText(FText::FromString(TEXT("PHONE")));
			Title->SetFont(MakeBMSPA(/*Size=*/18, /*Letter=*/3.f));
			Title->SetColorAndOpacity(FSlateColor(Cyan));
			if (UHorizontalBoxSlot* HS = HeaderRow->AddChildToHorizontalBox(Title))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			CloseBtn = WidgetTree->ConstructWidget<UButton>(
				UButton::StaticClass(), TEXT("CloseBtn"));
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
			UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("CloseLabel"));
			CloseLabel->SetText(FText::FromString(TEXT("×")));
			CloseLabel->SetFont(MakeBMSPA(/*Size=*/16));
			CloseLabel->SetColorAndOpacity(FSlateColor(CreamDim));
			CloseLabel->SetJustification(ETextJustify::Center);
			CloseBtn->SetContent(CloseLabel);
			USizeBox* CloseSize = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("CloseBtnSize"));
			CloseSize->SetWidthOverride(24.f);
			CloseSize->SetHeightOverride(24.f);
			CloseSize->AddChild(CloseBtn);
			if (UHorizontalBoxSlot* HS = HeaderRow->AddChildToHorizontalBox(CloseSize))
				HS->SetVerticalAlignment(VAlign_Center);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(HeaderRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// Header divider — 1 px cream line
		{
			UBorder* Div = WidgetTree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), TEXT("PhoneHeaderDiv"));
			Div->SetBrush(SolidBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.40f)));
			USizeBox* DivSize = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("PhoneHeaderDivSize"));
			DivSize->SetHeightOverride(1.f);
			DivSize->AddChild(Div);
			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DivSize))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── 2. Status row: CLOCK left, WALLET right ─────────────────────
		{
			UHorizontalBox* StatusRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("PhoneStatusRow"));

			// Clock + chapter label stacked vertically on the left
			UVerticalBox* ClockCol = WidgetTree->ConstructWidget<UVerticalBox>(
				UVerticalBox::StaticClass(), TEXT("PhoneClockCol"));

			ClockText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("ClockText"));
			ClockText->SetText(FText::FromString(TEXT("0:00")));
			ClockText->SetFont(MakeBMSPA(/*Size=*/26, /*Letter=*/2.f));
			ClockText->SetColorAndOpacity(FSlateColor(Cream));
			ClockCol->AddChildToVerticalBox(ClockText);

			ChapterLabelText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("ChapterLabelText"));
			ChapterLabelText->SetText(FText::FromString(TEXT("CH 0")));
			ChapterLabelText->SetFont(MakeBMSPA(/*Size=*/11, /*Letter=*/2.f));
			ChapterLabelText->SetColorAndOpacity(FSlateColor(CreamDim));
			ClockCol->AddChildToVerticalBox(ChapterLabelText);

			if (UHorizontalBoxSlot* HS = StatusRow->AddChildToHorizontalBox(ClockCol))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetVerticalAlignment(VAlign_Center);
			}

			WalletText = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("WalletText"));
			WalletText->SetText(FText::FromString(TEXT("C 0   N 0")));
			WalletText->SetFont(MakeBMSPA(/*Size=*/14, /*Letter=*/2.f));
			WalletText->SetColorAndOpacity(FSlateColor(Cyan));
			WalletText->SetJustification(ETextJustify::Right);
			if (UHorizontalBoxSlot* HS = StatusRow->AddChildToHorizontalBox(WalletText))
				HS->SetVerticalAlignment(VAlign_Center);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(StatusRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
		}

		// ── 3. Tab row: CONTACTS / NOTES ────────────────────────────────
		{
			UHorizontalBox* TabRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("PhoneTabRow"));

			auto MakeTabBtn = [&](FName Name, const FString& LabelText) -> UButton*
			{
				UButton* B = WidgetTree->ConstructWidget<UButton>(
					UButton::StaticClass(), Name);
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
				UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(
					UTextBlock::StaticClass(), NAME_None);
				Lbl->SetText(FText::FromString(LabelText));
				Lbl->SetFont(MakeBMSPA(/*Size=*/12, /*Letter=*/3.f));
				Lbl->SetColorAndOpacity(FSlateColor(Cream));
				Lbl->SetJustification(ETextJustify::Center);
				B->SetContent(Lbl);
				return B;
			};

			ContactsTabBtn = MakeTabBtn(TEXT("ContactsTabBtn"), TEXT("CONTACTS"));
			NotesTabBtn    = MakeTabBtn(TEXT("NotesTabBtn"),    TEXT("NOTES"));

			if (UHorizontalBoxSlot* HS = TabRow->AddChildToHorizontalBox(ContactsTabBtn))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			if (UHorizontalBoxSlot* HS = TabRow->AddChildToHorizontalBox(NotesTabBtn))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(TabRow))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── 4. Scroll content area (placeholder) ────────────────────────
		{
			ContentScroll = WidgetTree->ConstructWidget<UScrollBox>(
				UScrollBox::StaticClass(), TEXT("ContentScroll"));
			ContentScroll->SetAnimateWheelScrolling(true);

			ContentPlaceholder = WidgetTree->ConstructWidget<UTextBlock>(
				UTextBlock::StaticClass(), TEXT("ContentPlaceholder"));
			ContentPlaceholder->SetText(FText::FromString(
				TEXT("No contacts yet.\n\nCharacters you meet will appear here.")));
			ContentPlaceholder->SetFont(MakeRodin(/*Size=*/13));
			ContentPlaceholder->SetColorAndOpacity(FSlateColor(CreamDim));
			ContentPlaceholder->SetAutoWrapText(true);
			ContentScroll->AddChild(ContentPlaceholder);

			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(ContentScroll))
			{
				VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
			}
		}

		// Footer divider
		{
			UBorder* Div = WidgetTree->ConstructWidget<UBorder>(
				UBorder::StaticClass(), TEXT("PhoneFooterDiv"));
			Div->SetBrush(SolidBrush(FLinearColor(0.318f, 0.933f, 0.988f, 0.40f)));
			USizeBox* DivSize = WidgetTree->ConstructWidget<USizeBox>(
				USizeBox::StaticClass(), TEXT("PhoneFooterDivSize"));
			DivSize->SetHeightOverride(1.f);
			DivSize->AddChild(Div);
			if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DivSize))
				VS->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		}

		// ── 5. Action row: CALL · TEXT (disabled until ship) ───────────
		{
			UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("PhoneActionRow"));

			auto MakeActionBtn = [&](FName Name, const FString& LabelText) -> UButton*
			{
				UButton* B = WidgetTree->ConstructWidget<UButton>(
					UButton::StaticClass(), Name);
				FButtonStyle BS;
				BS.Normal   = RoundedBrush(FLinearColor(0.039f, 0.043f, 0.078f, 0.45f),
				                           FLinearColor(0.945f, 0.929f, 0.851f, 0.25f),
				                           1.f, 4.f);
				BS.Hovered  = BS.Normal;
				BS.Pressed  = BS.Normal;
				BS.Disabled = BS.Normal;
				B->SetStyle(BS);
				B->SetIsEnabled(false);
				UTextBlock* Lbl = WidgetTree->ConstructWidget<UTextBlock>(
					UTextBlock::StaticClass(), NAME_None);
				Lbl->SetText(FText::FromString(LabelText));
				Lbl->SetFont(MakeBMSPA(/*Size=*/12, /*Letter=*/3.f));
				Lbl->SetColorAndOpacity(FSlateColor(CreamDim));
				Lbl->SetJustification(ETextJustify::Center);
				B->SetContent(Lbl);
				return B;
			};

			CallBtn = MakeActionBtn(TEXT("CallBtn"), TEXT("CALL"));
			TextBtn = MakeActionBtn(TEXT("TextBtn"), TEXT("TEXT"));

			if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(CallBtn))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(0.f, 0.f, 4.f, 0.f));
			}
			if (UHorizontalBoxSlot* HS = ActionRow->AddChildToHorizontalBox(TextBtn))
			{
				HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				HS->SetPadding(FMargin(4.f, 0.f, 0.f, 0.f));
			}

			Column->AddChildToVerticalBox(ActionRow);
		}
	}

	return Super::Initialize();
}

void UEclipsePhoneWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind clicks (the close X + tab buttons; CALL/TEXT stay disabled in MVP).
	if (CloseBtn)
	{
		CloseBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		CloseBtn->OnClicked.AddDynamic(this, &UEclipsePhoneWidget::OnCloseClicked);
	}
	if (ContactsTabBtn)
	{
		ContactsTabBtn->OnClicked.AddDynamic(this, &UEclipsePhoneWidget::OnContactsTabClicked);
	}
	if (NotesTabBtn)
	{
		NotesTabBtn->OnClicked.AddDynamic(this, &UEclipsePhoneWidget::OnNotesTabClicked);
	}

	ApplyTab(EPhoneTab::Contacts);
	RefreshFace();
}

void UEclipsePhoneWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);

	// The phone face is real-time: clock + wallet update every tick so when
	// the dialogue +20s bump fires or coins land from a pickup, the player
	// sees the change immediately. The phone does NOT pause the world
	// (look-but-the-world-continues design), so the clock literally ticks
	// up while the phone is open.
	RefreshFace();
}

void UEclipsePhoneWidget::RefreshFace()
{
	UGameInstance* GI = GetGameInstance();
	UEclipseGameStateSubsystem* GS = GI
		? GI->GetSubsystem<UEclipseGameStateSubsystem>()
		: nullptr;
	if (!GS) return;

	if (ClockText)        ClockText->SetText(GS->GetChapterClockText());
	if (ChapterLabelText) ChapterLabelText->SetText(GS->GetChapterLabelText());
	if (WalletText)
	{
		// ASCII-only labels — earlier ₡/$ glyphs triggered missing-glyph
		// fallback on the Cantarell font in this build and added per-frame
		// log noise. Keep simple until we ship a font with full coverage.
		WalletText->SetText(FText::FromString(
			FString::Printf(TEXT("C %d   N %d"), GS->Coins, GS->Notes)));
	}
}

void UEclipsePhoneWidget::ApplyTab(EPhoneTab Tab)
{
	using namespace EclipseUI;

	ActiveTab = Tab;
	// Visual cue: brighten the active tab's label tint, dim the inactive.
	auto TintLabel = [](UButton* Btn, FLinearColor Tint)
	{
		if (!Btn) return;
		if (UTextBlock* Lbl = Cast<UTextBlock>(Btn->GetContent()))
		{
			Lbl->SetColorAndOpacity(FSlateColor(Tint));
		}
	};
	TintLabel(ContactsTabBtn, Tab == EPhoneTab::Contacts ? Cyan : CreamDim);
	TintLabel(NotesTabBtn,    Tab == EPhoneTab::Notes    ? Cyan : CreamDim);

	if (ContentPlaceholder)
	{
		ContentPlaceholder->SetText(FText::FromString(
			Tab == EPhoneTab::Contacts
				? TEXT("No contacts yet.\n\nCharacters you meet will appear here.")
				: TEXT("No notes yet.\n\nQuest log + memos will appear here.")));
	}
}

void UEclipsePhoneWidget::OnCloseClicked()        { Close(); }
void UEclipsePhoneWidget::OnContactsTabClicked()  { ApplyTab(EPhoneTab::Contacts); }
void UEclipsePhoneWidget::OnNotesTabClicked()     { ApplyTab(EPhoneTab::Notes); }

FReply UEclipsePhoneWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// ESC or P closes — symmetric with the open binding so the same key
	// that summoned the panel also dismisses it.
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape || Key == EKeys::P)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// ─────────────────────────────────────────────────────────────
//  Static open / Close
// ─────────────────────────────────────────────────────────────

UEclipsePhoneWidget* UEclipsePhoneWidget::OpenForPlayer(APlayerController* PC)
{
	if (!PC) return nullptr;

	TSubclassOf<UEclipsePhoneWidget> Cls = UEclipsePhoneWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipsePhoneWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_Phone.WBP_Phone_C")))
	{
		Cls = BPClass;
	}

	UEclipsePhoneWidget* W = CreateWidget<UEclipsePhoneWidget>(PC, Cls, TEXT("Phone"));
	if (!W) return nullptr;

	W->AddToViewport(/*ZOrder=*/100);
	W->SetIsFocusable(true);
	W->SetKeyboardFocus();

	// NB: phone does NOT pause the world (unlike Inventory / Stats). The
	// design intent is "look-but-the-world-continues" — chapter clock keeps
	// ticking, NPCs keep their behaviour, music + ambient audio carry on.
	// Player input is locked via UIOnly + IgnoreMove/Look so the character
	// stops walking, but the world tick is untouched. This is also what
	// makes the clock readout on the phone face useful: you can sit and
	// watch the minutes pass.
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	UE_LOG(LogEclipse, Log, TEXT("Phone: opened for %s"), *PC->GetName());
	return W;
}

void UEclipsePhoneWidget::Close()
{
	APlayerController* PC = GetOwningPlayer();
	// No SetGamePaused(false) — we never paused on open.
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
	}
	RemoveFromParent();
}
