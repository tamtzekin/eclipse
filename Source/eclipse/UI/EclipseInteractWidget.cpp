// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseInteractWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Data/EclipseItemDefinition.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Items/EclipseItemActor.h"
#include "Subsystems/EclipseInteractSubsystem.h"
#include "Subsystems/EclipseDialogueSubsystem.h"

// ─────────────────────────────────────────────────────────────
//  Initialize — build tree before BindWidget pass
//
//  CSS source (#interact-3d):
//      color: #51eefc; font-size: 13px; font-family: BMSPA;
//      letter-spacing: 2px; text-shadow: 0 0 8px rgba(81,238,252,0.5);
//
//  We render at ~28pt because UE viewport screen-pixels are bigger than
//  HTML's 13px reads in practice, and the user requested it bigger.
// ─────────────────────────────────────────────────────────────

bool UEclipseInteractWidget::Initialize()
{
	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("PromptText"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
			UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
		WidgetTree->RootWidget = Root;

		PromptText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), TEXT("PromptText"));

		// Bottom-center, ~75% down — matches HTML #interact-3d which is positioned
		// at the screen-projected NPC head, but as a debug fallback we anchor at
		// 50%/85% so it's always visible while we develop.
		if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(PromptText))
		{
			CSlot->SetAnchors(FAnchors(0.5f, 0.85f, 0.5f, 0.85f));
			CSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			CSlot->SetAutoSize(true);
		}

		// Font: BMSPA, large, with letter-spacing (CSS letter-spacing: 2px)
		// Same treatment as the corner clock: cream glyphs, red outline
		// hugging the edges, plus a zero-offset shadow in the same red so
		// the halo thickens evenly instead of reading as a drop shadow.
		// Keeps the two pieces of world-facing HUD text in one voice.
		{
			FSlateFontInfo F = EclipseUI::MakeBMSPA(/*Size=*/20, /*LetterSpacingPx=*/2.f);
			F.OutlineSettings.OutlineSize = 2;
			F.OutlineSettings.OutlineColor = EclipseUI::DialogueRed.CopyWithNewOpacity(0.85f);
			F.OutlineSettings.bApplyOutlineToDropShadows = true;
			PromptText->SetFont(F);
		}
		PromptText->SetColorAndOpacity(FSlateColor(EclipseUI::Cream));
		PromptText->SetJustification(ETextJustify::Center);

		// Glow text-shadow: rgba(81,238,252,0.5) at 8px blur — Slate single-shadow
		// approximation: cyan-tinted shadow with a small offset.
		PromptText->SetShadowOffset(FVector2D::ZeroVector);
		PromptText->SetShadowColorAndOpacity(EclipseUI::DialogueRed.CopyWithNewOpacity(0.85f));
	}

	return Super::Initialize();
}

// Built lazily into whatever canvas the widget ended up with, so it works
// whether the tree came from the WBP or the C++ fallback above.
void UEclipseInteractWidget::EnsurePickupCard()
{
	using namespace EclipseUI;
	if (PickupCard || !WidgetTree) return;
	UCanvasPanel* Root = Cast<UCanvasPanel>(WidgetTree->RootWidget);
	if (!Root) return;

	PickupCard = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PickupCard"));
	// No card and no frame: what's shown is the object, cut out. The old
	// rounded brush drew a translucent black box with a white rectangle
	// round it, which is the "square" rather than the item's shape.
	{
		FSlateBrush Clear;
		Clear.DrawAs    = ESlateBrushDrawType::NoDrawType;
		Clear.TintColor = FSlateColor(FLinearColor::Transparent);
		PickupCard->SetBrush(Clear);
	}
	PickupCard->SetPadding(FMargin(5.f));
	PickupCard->SetRenderOpacity(0.f);
	PickupCard->SetVisibility(ESlateVisibility::Collapsed);

	// Outline copies first (behind), real icon last (on top).
	UOverlay* Stack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), NAME_None);
	PickupOutline.Reset();
	const FVector2D Dirs[] = {
		{-1,-1}, {0,-1}, {1,-1},
		{-1, 0},         {1, 0},
		{-1, 1}, {0, 1}, {1, 1},
	};
	for (const FVector2D& D : Dirs)
	{
		UImage* O = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), NAME_None);
		O->SetRenderTranslation(D * PickupOutlinePx);
		Stack->AddChildToOverlay(O);
		PickupOutline.Add(O);
	}

	PickupImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PickupImage"));
	Stack->AddChildToOverlay(PickupImage);
	PickupCard->SetContent(Stack);

	// Just above the interact prompt, same column — the eye is already
	// there when the pickup happens.
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(PickupCard))
	{
		S->SetAnchors(FAnchors(0.5f, 0.85f, 0.5f, 0.85f));
		S->SetAlignment(FVector2D(0.5f, 1.f));
		S->SetPosition(FVector2D(0.f, -26.f));
		S->SetSize(FVector2D(96.f, 96.f));
	}
}

void UEclipseInteractWidget::HandleItemPickedUp(FName ItemId)
{
	EnsurePickupCard();
	if (!PickupCard || !PickupImage) return;

	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	// No baked render for this row means nothing worth showing — a blank
	// framed square says less than no card at all.
	FEclipseItemRow Row;
	UTexture2D* Icon = GS->GetItemRow(ItemId, Row) && !Row.IconTexture.IsNull()
		? Row.IconTexture.LoadSynchronous() : nullptr;
	if (!Icon) return;

	// SetBrushFromTexture leaves the brush's ImageSize at its 32x32 default
	// and DrawAs at whatever it was; both have to be set explicitly or the
	// card renders as an empty grey square with the texture never drawn.
	FSlateBrush B;
	B.SetResourceObject(Icon);
	B.DrawAs = ESlateBrushDrawType::Image;
	B.ImageSize = FVector2D(86.f, 86.f);
	B.TintColor = FSlateColor(FLinearColor::White);
	PickupImage->SetBrush(B);
	PickupImage->SetDesiredSizeOverride(FVector2D(86.f, 86.f));

	// Same sprite, flat white, one step out in each direction — the union of
	// the eight reads as a stroke that follows the icon's own edge.
	FSlateBrush OutlineBrush = B;
	OutlineBrush.TintColor = FSlateColor(FLinearColor::White);
	for (UImage* O : PickupOutline)
	{
		if (!O) continue;
		O->SetBrush(OutlineBrush);
		O->SetDesiredSizeOverride(FVector2D(86.f, 86.f));
	}

	PickupCard->SetVisibility(ESlateVisibility::HitTestInvisible);
	PickupCard->SetRenderOpacity(0.f);          // faded in by NativeTick
	PickupCardTimer = PickupCardFadeSeconds + PickupCardHoldSeconds + PickupCardFadeSeconds;
}

// Anchors the label to the subject in world space. Runs every frame because
// the label has to follow both the subject and the camera.
void UEclipseInteractWidget::TickPromptPosition()
{
	if (!PromptText) return;

	AActor* Target = CachedNpc.IsValid() ? Cast<AActor>(CachedNpc.Get())
	              : (CachedItem.IsValid() ? CachedItem.Get() : nullptr);
	if (!Target) return;

	UCanvasPanelSlot* Slot = Cast<UCanvasPanelSlot>(PromptText->Slot);
	APlayerController* PC  = GetOwningPlayer();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Slot || !Pawn) return;   // WBP put the label outside a canvas — leave it put

	FVector Origin, Extent;
	if (const ACharacter* Char = Cast<ACharacter>(Target))
	{
		// Not GetActorBounds for NPCs: it folds in the screen-space speech
		// bubble's widget component, whose world bounds are meaningless and
		// would shove the label off the character. The capsule is the
		// silhouette the label is meant to clear.
		const UCapsuleComponent* Cap = Char->GetCapsuleComponent();
		const float R = Cap->GetScaledCapsuleRadius();
		Origin = Cap->GetComponentLocation();
		Extent = FVector(R, R, Cap->GetScaledCapsuleHalfHeight());
	}
	else
	{
		Target->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent);
	}

	// Push the label onto the player's side of the subject, clear of its
	// silhouette, and lift it just over the top. Circling the object walks
	// the label around it.
	FVector Toward = Pawn->GetActorLocation() - Origin;
	Toward.Z = 0.f;
	Toward = Toward.GetSafeNormal();
	const float Radius = FMath::Max(Extent.Size2D(), 12.f);
	const FVector Anchor = Origin
		+ Toward * (Radius + LabelStandoffCm)
		+ FVector(0.f, 0.f, Extent.Z + LabelLiftCm);

	FVector2D Screen;
	const bool bOnScreen = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
		PC, Anchor, Screen, /*bPlayerViewportRelative=*/false);

	// Behind the camera projects to a mirrored point in front of it, which
	// would park the label on the wrong side of the screen.
	PromptText->SetRenderOpacity(bOnScreen ? 1.f : 0.f);
	if (!bOnScreen) return;

	if (!bPromptSlotReady)
	{
		Slot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
		Slot->SetAlignment(FVector2D(0.5f, 0.5f));
		Slot->SetAutoSize(true);
		bPromptSlotReady = true;
	}
	Slot->SetPosition(Screen);
}

void UEclipseInteractWidget::NativeTick(const FGeometry& G, float DeltaTime)
{
	Super::NativeTick(G, DeltaTime);
	TickPromptPosition();
	if (PickupCardTimer <= 0.f || !PickupCard) return;

	PickupCardTimer -= DeltaTime;

	// Timeline runs fade-in, hold, fade-out. Reading it off the remaining
	// time means the tail is the fade-out and the head is the fade-in.
	const float Total     = PickupCardFadeSeconds + PickupCardHoldSeconds + PickupCardFadeSeconds;
	const float Elapsed   = Total - PickupCardTimer;
	const float FadeIn    = FMath::Clamp(Elapsed / PickupCardFadeSeconds, 0.f, 1.f);
	const float FadeOut   = FMath::Clamp(PickupCardTimer / PickupCardFadeSeconds, 0.f, 1.f);
	PickupCard->SetRenderOpacity(FMath::Min(FadeIn, FadeOut));
	if (PickupCardTimer <= 0.f)
	{
		PickupCard->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UEclipseInteractWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnItemPickedUp.AddDynamic(this, &UEclipseInteractWidget::HandleItemPickedUp);
	}

	// Start hidden
	SetVisibility(ESlateVisibility::Hidden);

	if (UEclipseInteractSubsystem* IS = GetWorld()->GetSubsystem<UEclipseInteractSubsystem>())
	{
		IS->OnNearTalkableChanged.AddDynamic(this, &UEclipseInteractWidget::HandleNearTalkableChanged);
		IS->OnNearItemChanged.AddDynamic(this, &UEclipseInteractWidget::HandleNearItemChanged);
		UE_LOG(LogEclipse, Log, TEXT("InteractWidget: bound to InteractSubsystem"));
	}
	else
	{
		UE_LOG(LogEclipse, Warning, TEXT("InteractWidget: failed to find InteractSubsystem"));
	}

	if (UEclipseDialogueSubsystem* DS = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr)
	{
		DS->OnDialogueOpened.AddDynamic(this, &UEclipseInteractWidget::HandleDialogueOpened);
		DS->OnDialogueClosed.AddDynamic(this, &UEclipseInteractWidget::HandleDialogueClosed);
		bDialogueOpen = DS->IsDialogueOpen();
	}
}

void UEclipseInteractWidget::NativeDestruct()
{
	if (UEclipseInteractSubsystem* IS = GetWorld() ? GetWorld()->GetSubsystem<UEclipseInteractSubsystem>() : nullptr)
	{
		IS->OnNearTalkableChanged.RemoveDynamic(this, &UEclipseInteractWidget::HandleNearTalkableChanged);
		IS->OnNearItemChanged.RemoveDynamic(this, &UEclipseInteractWidget::HandleNearItemChanged);
	}
	if (UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
	{
		if (UEclipseDialogueSubsystem* DS = GI->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			DS->OnDialogueOpened.RemoveDynamic(this, &UEclipseInteractWidget::HandleDialogueOpened);
			DS->OnDialogueClosed.RemoveDynamic(this, &UEclipseInteractWidget::HandleDialogueClosed);
		}
	}
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnItemPickedUp.RemoveDynamic(this, &UEclipseInteractWidget::HandleItemPickedUp);
	}
	Super::NativeDestruct();
}

void UEclipseInteractWidget::HandleNearTalkableChanged(AEclipseNpcCharacter* Npc)
{
	UE_LOG(LogEclipse, Log, TEXT("InteractWidget::HandleNearTalkableChanged → %s"),
		Npc ? *Npc->NpcName.ToString() : TEXT("(null)"));
	CachedNpc = Npc;
	RefreshPrompt();
}

void UEclipseInteractWidget::HandleNearItemChanged(AActor* Item)
{
	CachedItem = Item;
	RefreshPrompt();
}

void UEclipseInteractWidget::HandleDialogueOpened(AEclipseNpcCharacter* Npc)
{
	bDialogueOpen = true;
	RefreshPrompt();
}

void UEclipseInteractWidget::HandleDialogueClosed()
{
	bDialogueOpen = false;
	RefreshPrompt();
}

void UEclipseInteractWidget::RefreshPrompt()
{
	if (!PromptText) return;

	if (bDialogueOpen)
	{
		SetVisibility(ESlateVisibility::Hidden);
		return;
	}

	// Talkable NPC takes priority over item. Just the name — the "[E] TALK
	// TO ..." banner is gone; position next to the subject is what tells you
	// which thing the label is about.
	if (CachedNpc.IsValid())
	{
		PromptText->SetText(FText::FromString(CachedNpc->GetDisplayName().ToString().ToUpper()));
		SetVisibility(ESlateVisibility::HitTestInvisible);
		TickPromptPosition();   // place it before the first frame draws
		return;
	}

	if (CachedItem.IsValid())
	{
		FText ItemDisplay;
		if (AEclipseItemActor* IA = Cast<AEclipseItemActor>(CachedItem.Get()))
		{
			ItemDisplay = IA->DisplayName;
			// Fall back to the DT_Items row before the raw id. Placed actors
			// mostly leave DisplayName empty and let the row name them, so
			// without this the prompt read out the internal key —
			// "LOTUS_ECSTASY" instead of "Pack of Cigarettes".
			if (ItemDisplay.IsEmpty())
			{
				FEclipseItemRow Row;
				const UEclipseGameStateSubsystem* GS = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
				if (GS && GS->GetItemRow(IA->ItemId, Row) && !Row.DisplayName.IsEmpty())
				{
					ItemDisplay = Row.DisplayName;
				}
			}
			if (ItemDisplay.IsEmpty()) ItemDisplay = FText::FromName(IA->ItemId);
		}
		else
		{
			ItemDisplay = FText::FromString(CachedItem->GetName().ToUpper());
		}

		// Leading "x" marks the spot the label is pointing at — the name on
		// its own reads as scenery text rather than a thing you can take.
		PromptText->SetText(FText::FromString(
			FString::Printf(TEXT("x  %s"), *ItemDisplay.ToString().ToUpper())));
		SetVisibility(ESlateVisibility::HitTestInvisible);
		TickPromptPosition();
		return;
	}

	SetVisibility(ESlateVisibility::Hidden);
}
