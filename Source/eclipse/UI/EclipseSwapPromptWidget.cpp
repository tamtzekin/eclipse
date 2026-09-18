// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseSwapPromptWidget.h"
#include "eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Items/EclipseItemActor.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UEclipseSwapPromptWidget* UEclipseSwapPromptWidget::OpenForPickup(APlayerController* PC, AEclipseItemActor* Pickup)
{
	if (!PC || !Pickup) return nullptr;

	UGameInstance* GI = PC->GetGameInstance();
	UEclipseGameStateSubsystem* GS = GI ? GI->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return nullptr;

	// Nothing to trade — don't open a prompt whose only answer is "no".
	// The caller treats null as "pickup refused".
	const TArray<FName> Candidates = GS->GetSwapCandidates(Pickup->MakeRuntimeId());
	if (Candidates.Num() == 0) return nullptr;

	TSubclassOf<UEclipseSwapPromptWidget> Cls = UEclipseSwapPromptWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseSwapPromptWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_SwapPrompt.WBP_SwapPrompt_C")))
	{
		Cls = BPClass;
	}

	UEclipseSwapPromptWidget* W = CreateWidget<UEclipseSwapPromptWidget>(PC, Cls, TEXT("SwapPrompt"));
	if (!W) return nullptr;

	// Both set before AddToViewport so NativeConstruct can already read them.
	W->PendingPickup = Pickup;
	W->OutgoingId    = Candidates[0];   // hands before pockets

	W->AddToViewport(/*ZOrder=*/900);   // under the death overlay, over the HUD
	W->SetIsFocusable(true);

	// NO pause. The club keeps running while you decide — deliberate, so a
	// swap reads as something you do mid-stride rather than a menu you enter.
	// GameAndUI (not UIOnly) keeps movement alive underneath the cursor.
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	W->SetKeyboardFocus();

	UE_LOG(LogEclipse, Log, TEXT("SwapPrompt: '%s' <-> '%s'"),
		*W->OutgoingId.ToString(), *Pickup->ItemId.ToString());
	return W;
}

void UEclipseSwapPromptWidget::Close()
{
	if (bDismissed) return;
	bDismissed = true;

	if (APlayerController* PC = GetOwningPlayer())
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
	RemoveFromParent();
}

bool UEclipseSwapPromptWidget::Initialize()
{
	// Pure-C++ path: no BP archetype means no WidgetTree, so allocate one
	// for BuildFallbackTree to land in. Same shape as the death overlay.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transactional);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("CandidateRow"))))
	{
		BuildFallbackTree();
	}
	return Super::Initialize();
}

void UEclipseSwapPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BuildCandidateBoxes();
}

FReply UEclipseSwapPromptWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Esc reads as "keep what I've got" — the same as clicking the left box.
	// Handled here rather than left to the player controller so the prompt
	// doesn't fall through and open the pause menu underneath.
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnKeepOldClicked();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UEclipseSwapPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bDismissed) return;

	AEclipseItemActor* Pickup = PendingPickup.Get();
	if (!Pickup)
	{
		Close();   // item destroyed or level changed out from under us
		return;
	}

	APawn* Pawn = GetOwningPlayer() ? GetOwningPlayer()->GetPawn() : nullptr;
	if (!Pawn) return;

	// Same radius the interact prompt uses to offer the pickup in the first
	// place, with a little slack so standing exactly on the boundary doesn't
	// flicker the panel in and out.
	const float R = Pickup->PickupRadius * 1.25f;
	if (FVector::DistSquared(Pawn->GetActorLocation(), Pickup->GetActorLocation()) > R * R)
	{
		UE_LOG(LogEclipse, Log, TEXT("SwapPrompt: player left the pickup radius — closing"));
		Close();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Handlers — one per box, so neither needs to work out which was clicked
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseSwapPromptWidget::OnKeepOldClicked()
{
	UE_LOG(LogEclipse, Log, TEXT("SwapPrompt: kept '%s' — pickup left in the room"),
		*OutgoingId.ToString());
	Close();
}

void UEclipseSwapPromptWidget::OnTakeNewClicked()
{
	AEclipseItemActor* Pickup = PendingPickup.Get();
	if (!Pickup)
	{
		Close();
		return;
	}

	if (Pickup->TakeAfterSwap(OutgoingId))
	{
		UE_LOG(LogEclipse, Log, TEXT("SwapPrompt: swapped '%s' for '%s'"),
			*OutgoingId.ToString(), *Pickup->ItemId.ToString());
	}
	else
	{
		// Refused — usually the freed carrier is too small for the incoming
		// item. Nothing changed; closing leaves both where they were.
		UE_LOG(LogEclipse, Log, TEXT("SwapPrompt: swap of '%s' refused"), *OutgoingId.ToString());
	}
	Close();
}

// ─────────────────────────────────────────────────────────────────────────────
//  The two boxes — [ old ]  →  [ new ]
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseSwapPromptWidget::BuildCandidateBoxes()
{
	using namespace EclipseUI;

	if (!CandidateRow || !WidgetTree) return;

	CandidateRow->ClearChildren();
	OldBox = nullptr;
	NewBox = nullptr;

	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS || !PendingPickup.IsValid() || OutgoingId.IsNone()) return;

	const FName IncomingId = PendingPickup->MakeRuntimeId();

	// One box. bIsNew tints it — the thing you'd gain reads in the game's
	// red, the thing you'd give up stays neutral.
	auto MakeBox = [&](const FText& Label, FName WidgetName, bool bIsNew) -> UButton*
	{
		UButton* Box = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);

		// Red box = what you'd gain, white box = what you'd give up. The
		// outline is what names each side; the fill only says how hot the
		// hover is, so the two never blur into the same grey.
		const FLinearColor Edge = bIsNew ? DialogueRed : Cream;
		auto BoxBrush = [&Edge](float Alpha)
		{
			return RoundedBrush(Edge.CopyWithNewOpacity(Alpha), Edge, 1.5f, 0.f);
		};
		FButtonStyle BS;
		BS.Normal   = BoxBrush(0.10f);
		BS.Hovered  = BoxBrush(0.34f);
		BS.Pressed  = BoxBrush(0.52f);
		BS.Disabled = BoxBrush(0.04f);
		Box->SetStyle(BS);
		Box->SetClickMethod(EButtonClickMethod::MouseDown);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(Label);
		T->SetFont(MakeBMSPA(20, 2.f));
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetJustification(ETextJustify::Center);
		T->SetAutoWrapText(true);
		Box->SetContent(T);

		// Each box gets a caption so the two sides name themselves.
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Col"), *WidgetName.ToString())));

		UTextBlock* Caption = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Caption"), *WidgetName.ToString())));
		Caption->SetText(FText::FromString(bIsNew ? TEXT("Swap with:") : TEXT("Holding:")));
		Caption->SetFont(MakeRodin(14));
		Caption->SetColorAndOpacity(FSlateColor(CreamDim));
		Caption->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* VS = Col->AddChildToVerticalBox(Caption))
		{
			VS->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			VS->SetHorizontalAlignment(HAlign_Center);
			VS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}
		if (UVerticalBoxSlot* VS = Col->AddChildToVerticalBox(WrapWithInnerGlow(WidgetTree, Box, /*EdgePx=*/12.f)))
		{
			VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			VS->SetHorizontalAlignment(HAlign_Fill);
		}

		if (UHorizontalBoxSlot* HS = CandidateRow->AddChildToHorizontalBox(Col))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(6.f, 0.f));
			HS->SetVerticalAlignment(VAlign_Fill);
		}
		return Box;
	};

	OldBox = MakeBox(GS->GetItemDisplayName(OutgoingId), TEXT("OldBox"), /*bIsNew=*/false);
	OldBox->OnClicked.AddDynamic(this, &UEclipseSwapPromptWidget::OnKeepOldClicked);

	NewBox = MakeBox(GS->GetItemDisplayName(IncomingId), TEXT("NewBox"), /*bIsNew=*/true);
	NewBox->OnClicked.AddDynamic(this, &UEclipseSwapPromptWidget::OnTakeNewClicked);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout — fallback tree
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseSwapPromptWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// No full-screen dim: the game is still running underneath and blacking
	// it out would read as a pause the prompt doesn't actually apply.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SwapPanel"));
	Panel->SetBrush(RoundedBrush(FLinearColor(0.039f, 0.043f, 0.059f, 0.97f), DialogueRed, 1.f, 0.f));
	Panel->SetPadding(FMargin(18.f, 14.f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		// Low and centred — near the pickup, out of the way of the HUD
		// cluster in the bottom-right.
		S->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		S->SetAlignment(FVector2D(0.5f, 1.f));
		S->SetPosition(FVector2D(0.f, -140.f));
		S->SetSize(FVector2D(560.f, 120.f));
		S->SetZOrder(1);
	}

	// Filled by BuildCandidateBoxes: [old] -> [new].
	CandidateRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("CandidateRow"));
	Panel->SetContent(CandidateRow);
}
