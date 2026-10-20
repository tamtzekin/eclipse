// Copyright (c) ECLIPSE. All Rights Reserved.

#include "UI/EclipseCharacterCreationWidget.h"
#include "EclipseUiStyle.h"
#include "eclipse.h"
#include "Data/EclipseDanceStyle.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// Card order is the dance styles in enum order, then the four stats.
	struct FCardDef { const TCHAR* Key; const TCHAR* Name; const TCHAR* Blurb; bool bDance; bool bLocked; };
	const FCardDef CardDefs[] = {
		{ TEXT("hakken"),    TEXT("HAKKEN"),    TEXT("Stamping, both feet, straight down into the floor. Heavy. The Lad's style, so you'll meet it first."), true,  false },
		{ TEXT("muzzing"),   TEXT("MUZZING"),   TEXT("Loose at the shoulders, all upper body. Light. Reads as confidence whether or not you have any."), true,  false },
		{ TEXT("liquid"),    TEXT("LIQUID"),    TEXT("Hands like water through the dark. Someone in this city dances it. Not you, not yet."), true,  true  },
		{ TEXT("tektonik"),  TEXT("TEKTONIK"),  TEXT("Elbows and wrists, fast and vain. You'd have to be shown."), true,  true  },
		{ TEXT("jumpstyle"), TEXT("JUMPSTYLE"), TEXT("Legs, and the floor giving way under them. Found further out than you've been."), true,  true  },
		{ TEXT("shuffle"),   TEXT("SHUFFLE"),   TEXT("Small quick steps, barely leaving the ground. Locked until someone teaches you."), true,  true  },
		{ TEXT("aesthetics"),  TEXT("AESTHETICS"),   TEXT("Taste, fit, presentation. What you notice about a room and what it notices about you."), false, false },
		{ TEXT("rhythm"),      TEXT("RHYTHM"),       TEXT("Flow and timing. Holding a beat when nothing else is holding still."), false, false },
		{ TEXT("zen"),         TEXT("ZEN"),          TEXT("Composure. Saying nothing and having it land."), false, false },
		{ TEXT("psychedelics"),TEXT("PSYCHEDELICS"), TEXT("Openness to weird input. Seeing the thing that isn't officially there."), false, false },
	};
	constexpr int32 CardCount = UE_ARRAY_COUNT(CardDefs);
	constexpr int32 DanceCards = 6;

	FName CardName(int32 i, const TCHAR* Suffix) { return FName(*FString::Printf(TEXT("Card%d%s"), i, Suffix)); }
}

UEclipseCharacterCreationWidget* UEclipseCharacterCreationWidget::OpenForPlayer(APlayerController* PC, FName StartLevel)
{
	if (!PC) return nullptr;

	TSubclassOf<UEclipseCharacterCreationWidget> Cls = StaticClass();
	if (UClass* BP = LoadClass<UEclipseCharacterCreationWidget>(nullptr, TEXT("/Game/Justin/UI/WBP_CharacterCreation.WBP_CharacterCreation_C"))) Cls = BP;
	UEclipseCharacterCreationWidget* W = CreateWidget<UEclipseCharacterCreationWidget>(PC, Cls);
	if (!W) return nullptr;
	W->StartLevelName = StartLevel;
	W->AddToViewport(/*ZOrder=*/500);
	W->SetKeyboardFocus();

	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	return W;
}

bool UEclipseCharacterCreationWidget::Initialize()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("Card0"))))
	{
		BuildTree(WidgetTree);
	}
	return Super::Initialize();
}

void UEclipseCharacterCreationWidget::BuildTree(UWidgetTree* Tree)
{
	using namespace EclipseUI;

	UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	Tree->RootWidget = Root;

	UBorder* Panel = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CreationPanel"));
	Panel->SetBrush(SolidBrush(FLinearColor(0.02f, 0.02f, 0.025f, 1.f)));
	Panel->SetPadding(FMargin(70.f, 54.f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CreationColumn"));
	Panel->SetContent(Column);

	auto Text = [Tree](FName Name, const FSlateFontInfo& Font, const FLinearColor& Color, const FString& Str) -> UTextBlock*
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		T->SetFont(Font);
		T->SetColorAndOpacity(FSlateColor(Color));
		T->SetText(FText::FromString(Str));
		return T;
	};

	// ── Header: who you are, and what you have left to spend ──
	UHorizontalBox* Header = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CreationHeader"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Header)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 34.f));
	UTextBlock* Title = Text(TEXT("CreationTitle"), MakeRodin(38), Cream, TEXT("BEFORE THE NIGHT"));
	if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(Title))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetVerticalAlignment(VAlign_Bottom);
	}
	UHorizontalBox* PointsRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PointsRow"));
	if (UHorizontalBoxSlot* S = Header->AddChildToHorizontalBox(PointsRow)) S->SetVerticalAlignment(VAlign_Bottom);
	UTextBlock* PointsLabel = Text(TEXT("PointsLabel"), MakeFragmentMono(13), CreamDim, TEXT("POINTS LEFT  "));
	if (UHorizontalBoxSlot* S = PointsRow->AddChildToHorizontalBox(PointsLabel)) S->SetVerticalAlignment(VAlign_Bottom);
	UTextBlock* PointsNum = Text(TEXT("PointsValue"), MakeRodin(38), Cyan, TEXT("10"));
	if (UHorizontalBoxSlot* S = PointsRow->AddChildToHorizontalBox(PointsNum)) S->SetVerticalAlignment(VAlign_Bottom);

	// ── The grid: one card per style, one per stat ──
	const auto Section = [&](const TCHAR* Label, FName GridName, int32 First, int32 Last)
	{
		UTextBlock* Head = Text(FName(*FString::Printf(TEXT("%s_Head"), *GridName.ToString())), MakeBMSPA(15, 3.f), DialogueRed, Label);
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Head)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

		UUniformGridPanel* Grid = Tree->ConstructWidget<UUniformGridPanel>(UUniformGridPanel::StaticClass(), GridName);
		Grid->SetSlotPadding(FMargin(7.f));
		if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Grid)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 30.f));

		for (int32 i = First; i <= Last; ++i)
		{
			const FCardDef& Def = CardDefs[i];

			UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), CardName(i, TEXT("")));
			FButtonStyle BS;
			BS.Normal = BS.Hovered = BS.Pressed = BS.Disabled = SolidBrush(FLinearColor::Transparent);
			Btn->SetStyle(BS);
			Btn->SetClickMethod(EButtonClickMethod::MouseDown);
			if (UUniformGridSlot* GS = Grid->AddChildToUniformGrid(Btn, 0, i - First))
			{
				GS->SetHorizontalAlignment(HAlign_Fill);
				GS->SetVerticalAlignment(VAlign_Fill);
			}

			UBorder* Card = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), CardName(i, TEXT("_Bg")));
			Card->SetBrush(RoundedBrush(SlotGrey, FLinearColor::Transparent, 0.f, 10.f));
			Card->SetPadding(FMargin(16.f, 18.f));
			Card->SetVerticalAlignment(VAlign_Top);
			Btn->SetContent(Card);

			USizeBox* Height = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), CardName(i, TEXT("_Size")));
			Height->SetHeightOverride(132.f);
			Card->SetContent(Height);

			UVerticalBox* CardCol = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), CardName(i, TEXT("_Col")));
			Height->SetContent(CardCol);

			UTextBlock* Name = Text(CardName(i, TEXT("_Name")), MakeBMSPA(17, 1.f),
				Def.bDance ? EclipseDance::StyleColor((EEclipseDanceStyle)i) : Cream, Def.Name);
			if (UVerticalBoxSlot* S = CardCol->AddChildToVerticalBox(Name)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));

			// Level as a row of pips rather than a number: it's a sheet, not a spreadsheet.
			UHorizontalBox* Pips = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), CardName(i, TEXT("_Pips")));
			if (UVerticalBoxSlot* S = CardCol->AddChildToVerticalBox(Pips)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
			for (int32 p = 0; p < MaxLevel; ++p)
			{
				USizeBox* PipSize = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), NAME_None);
				PipSize->SetWidthOverride(15.f);
				PipSize->SetHeightOverride(5.f);
				UBorder* Pip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), CardName(i, *FString::Printf(TEXT("_Pip%d"), p)));
				Pip->SetBrush(SolidBrush(FLinearColor(1.f, 1.f, 1.f, 0.14f)));
				PipSize->SetContent(Pip);
				if (UHorizontalBoxSlot* S = Pips->AddChildToHorizontalBox(PipSize)) S->SetPadding(FMargin(0.f, 0.f, 5.f, 0.f));
			}

			UTextBlock* Note = Text(CardName(i, TEXT("_Note")), MakeFragmentMono(11), CreamDim,
				Def.bLocked ? TEXT("LOCKED") : TEXT(""));
			CardCol->AddChildToVerticalBox(Note);
		}
	};
	Section(TEXT("DANCE"), TEXT("DanceGrid"), 0, DanceCards - 1);
	Section(TEXT("YOU"), TEXT("StatGrid"), DanceCards, CardCount - 1);

	// ── Footer: what the picked card is, and the two buttons that act on it ──
	UHorizontalBox* Footer = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CreationFooter"));
	if (UVerticalBoxSlot* S = Column->AddChildToVerticalBox(Footer))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetVerticalAlignment(VAlign_Bottom);
	}

	UBorder* Tip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TooltipPanel"));
	Tip->SetBrush(RoundedBrush(FLinearColor(1.f, 1.f, 1.f, 0.05f), FLinearColor::Transparent, 0.f, 10.f));
	Tip->SetPadding(FMargin(22.f, 18.f));
	Tip->SetVerticalAlignment(VAlign_Top);
	if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(Tip))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetVerticalAlignment(VAlign_Bottom);
	}
	UVerticalBox* TipCol = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TooltipColumn"));
	Tip->SetContent(TipCol);
	UTextBlock* TipTitle = Text(TEXT("TooltipTitle"), MakeBMSPA(16, 2.f), Cream, TEXT(""));
	if (UVerticalBoxSlot* S = TipCol->AddChildToVerticalBox(TipTitle)) S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	UTextBlock* TipBody = Text(TEXT("TooltipBody"), MakeFragmentMono(13), CreamDim, TEXT(""));
	TipBody->SetAutoWrapText(true);
	TipCol->AddChildToVerticalBox(TipBody);

	UVerticalBox* Actions = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CreationActions"));
	if (UHorizontalBoxSlot* S = Footer->AddChildToHorizontalBox(Actions))
	{
		S->SetPadding(FMargin(26.f, 0.f, 0.f, 0.f));
		S->SetVerticalAlignment(VAlign_Bottom);
	}
	const auto Action = [&](const TCHAR* Label, FName WidgetName, const FLinearColor& Fill, const FLinearColor& Ink) -> UButton*
	{
		UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
		FButtonStyle BS;
		BS.Normal = RoundedBrush(Fill, FLinearColor::Transparent, 0.f, 8.f);
		BS.Hovered = RoundedBrush(Fill * 1.25f, FLinearColor::Transparent, 0.f, 8.f);
		BS.Pressed = BS.Hovered;
		BS.Disabled = RoundedBrush(Fill * 0.4f, FLinearColor::Transparent, 0.f, 8.f);
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);
		UTextBlock* T = Text(FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())), MakeBMSPA(16, 3.f), Ink, Label);
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);
		if (UButtonSlot* BSlot = Cast<UButtonSlot>(T->Slot)) BSlot->SetPadding(FMargin(42.f, 16.f));
		if (UVerticalBoxSlot* S = Actions->AddChildToVerticalBox(Btn)) S->SetPadding(FMargin(0.f, 8.f, 0.f, 0.f));
		return Btn;
	};
	Action(TEXT("UPGRADE"), TEXT("UpgradeBtn"), Cyan, FLinearColor::Black);
	Action(TEXT("BEGIN"), TEXT("BeginBtn"), FLinearColor(1.f, 1.f, 1.f, 0.10f), Cream);
}

void UEclipseCharacterCreationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	Cards.Reset();
	for (const FCardDef& Def : CardDefs)
	{
		FEclipseCreationCard C;
		C.Key = FName(Def.Key);
		C.Name = Def.Name;
		C.Blurb = Def.Blurb;
		C.bDance = Def.bDance;
		C.bLocked = Def.bLocked;
		Cards.Add(C);
	}

	// One handler per card: UButton::OnClicked carries no payload, so bind OnCard<i> by name.
	for (int32 i = 0; i < CardCount; ++i)
	{
		if (UButton* Btn = Cast<UButton>(WidgetTree->FindWidget(CardName(i, TEXT("")))))
		{
			FScriptDelegate D;
			D.BindUFunction(this, FName(*FString::Printf(TEXT("OnCard%d"), i)));
			Btn->OnClicked.Add(D);
		}
	}
	if (UpgradeBtn) UpgradeBtn->OnClicked.AddDynamic(this, &UEclipseCharacterCreationWidget::OnUpgrade);
	if (BeginBtn) BeginBtn->OnClicked.AddDynamic(this, &UEclipseCharacterCreationWidget::OnBegin);

	Select(0);
}

void UEclipseCharacterCreationWidget::OnCard0() { Select(0); } void UEclipseCharacterCreationWidget::OnCard1() { Select(1); }
void UEclipseCharacterCreationWidget::OnCard2() { Select(2); } void UEclipseCharacterCreationWidget::OnCard3() { Select(3); }
void UEclipseCharacterCreationWidget::OnCard4() { Select(4); } void UEclipseCharacterCreationWidget::OnCard5() { Select(5); }
void UEclipseCharacterCreationWidget::OnCard6() { Select(6); } void UEclipseCharacterCreationWidget::OnCard7() { Select(7); }
void UEclipseCharacterCreationWidget::OnCard8() { Select(8); } void UEclipseCharacterCreationWidget::OnCard9() { Select(9); }

void UEclipseCharacterCreationWidget::Select(int32 Index)
{
	if (!Cards.IsValidIndex(Index)) return;
	Selected = Index;
	Refresh();
}

void UEclipseCharacterCreationWidget::OnUpgrade()
{
	if (!Cards.IsValidIndex(Selected)) return;
	FEclipseCreationCard& C = Cards[Selected];
	if (C.bLocked || Points <= 0 || C.Level >= MaxLevel) return;
	--Points;
	++C.Level;
	Refresh();
}

void UEclipseCharacterCreationWidget::OnBegin()
{
	// Nothing is spent until they commit, so backing out of the screen leaves the save alone.
	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		for (int32 i = 0; i < Cards.Num(); ++i)
		{
			const FEclipseCreationCard& C = Cards[i];
			if (C.Level <= 0) continue;
			if (C.bDance) GS->SetDanceStyleLevel((EEclipseDanceStyle)i, C.Level);
			else GS->ApplyStatDelta(C.Key, C.Level);
		}
	}
	UE_LOG(LogEclipse, Log, TEXT("Creation: starting %s with %d point(s) unspent"), *StartLevelName.ToString(), Points);

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	RemoveFromParent();
	if (UWorld* W = GetWorld()) UGameplayStatics::OpenLevel(W, StartLevelName);
}

void UEclipseCharacterCreationWidget::Refresh()
{
	using namespace EclipseUI;

	if (PointsValue) PointsValue->SetText(FText::FromString(FString::Printf(TEXT("%02d"), Points)));

	for (int32 i = 0; i < Cards.Num(); ++i)
	{
		const FEclipseCreationCard& C = Cards[i];
		const bool bPicked = i == Selected;
		const FLinearColor Accent = C.bDance ? EclipseDance::StyleColor((EEclipseDanceStyle)i) : Cyan;

		if (UBorder* Bg = Cast<UBorder>(WidgetTree->FindWidget(CardName(i, TEXT("_Bg")))))
		{
			// Picked card lifts off the panel; locked ones stay sunk into it.
			const FLinearColor Fill = C.bLocked ? FLinearColor(1.f, 1.f, 1.f, 0.03f) : bPicked ? FLinearColor(1.f, 1.f, 1.f, 0.13f) : SlotGrey;
			Bg->SetBrush(RoundedBrush(Fill, bPicked ? Accent : FLinearColor::Transparent, bPicked ? 2.f : 0.f, 10.f));
		}
		if (UTextBlock* Name = Cast<UTextBlock>(WidgetTree->FindWidget(CardName(i, TEXT("_Name")))))
		{
			Name->SetColorAndOpacity(FSlateColor(C.bLocked ? CreamDim.CopyWithNewOpacity(0.35f) : C.bDance ? Accent : Cream));
		}
		for (int32 p = 0; p < MaxLevel; ++p)
		{
			if (UBorder* Pip = Cast<UBorder>(WidgetTree->FindWidget(CardName(i, *FString::Printf(TEXT("_Pip%d"), p)))))
			{
				Pip->SetBrush(SolidBrush(p < C.Level ? Accent : FLinearColor(1.f, 1.f, 1.f, 0.14f)));
			}
		}
		if (UTextBlock* Note = Cast<UTextBlock>(WidgetTree->FindWidget(CardName(i, TEXT("_Note")))))
		{
			Note->SetText(FText::FromString(C.bLocked ? TEXT("LOCKED") : C.Level > 0 ? FString::Printf(TEXT("LEVEL %d"), C.Level) : TEXT("UNTRAINED")));
		}
	}

	if (!Cards.IsValidIndex(Selected)) return;
	const FEclipseCreationCard& Sel = Cards[Selected];
	if (TooltipTitle) TooltipTitle->SetText(FText::FromString(Sel.Name));
	if (TooltipBody)
	{
		TooltipBody->SetText(FText::FromString(Sel.bLocked
			? FString::Printf(TEXT("%s  (You can't take this one yet.)"), *Sel.Blurb)
			: Sel.Blurb));
	}
	const bool bCanSpend = !Sel.bLocked && Points > 0 && Sel.Level < MaxLevel;
	if (UpgradeBtn) UpgradeBtn->SetIsEnabled(bCanSpend);
	if (UpgradeBtn_Label)
	{
		UpgradeBtn_Label->SetText(FText::FromString(Sel.bLocked ? TEXT("LOCKED") : Points <= 0 ? TEXT("NO POINTS") : TEXT("UPGRADE")));
	}
	if (BeginBtn_Label) BeginBtn_Label->SetText(FText::FromString(Points > 0 ? TEXT("BEGIN ANYWAY") : TEXT("BEGIN")));
}
