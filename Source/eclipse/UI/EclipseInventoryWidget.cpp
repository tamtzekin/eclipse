// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseInventoryWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
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
#include "Components/SizeBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Data/EclipseItemDefinition.h"
#include "Data/EclipseClothingDefinition.h"
#include "Items/EclipseItemActor.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory chip widget — one per item in the active tab. Holds the item ID
//  + a back-pointer to its owning inventory so the button click routes to
//  UEclipseInventoryWidget::SelectItem (UFUNCTION dynamic delegates can't
//  bind lambdas, so per-chip selection needs its own per-chip class).
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseInventoryChipWidget::InitChip(UUserWidget* InOwner, FName InItemId,
	bool bInEquipped, const FString& IconText, const FString& NameText, const FLinearColor& TintColor,
	int32 InSlotIndex)
{
	Owner       = InOwner;
	ItemId      = InItemId;
	bIsEquipped = bInEquipped;
	Tint        = TintColor;
	SlotIndex   = InSlotIndex;
	bIsEmpty    = false;
	if (ChipIconText) ChipIconText->SetText(FText::FromString(IconText));
	if (ChipNameText) ChipNameText->SetText(FText::FromString(NameText.ToUpper()));
	SetSelectedVisual(false);
}

void UEclipseInventoryChipWidget::InitEmptySlot(UUserWidget* InOwner, int32 InSlotIndex)
{
	using namespace EclipseUI;

	Owner       = InOwner;
	ItemId      = NAME_None;
	bIsEquipped = false;
	Tint        = LinkBlue;
	SlotIndex   = InSlotIndex;
	bIsEmpty    = true;

	// Empty cell visual — dim outline, no labels. Re-style the inherited
	// frame brush so it matches the prior plain UBorder look.
	if (ChipIconText) ChipIconText->SetText(FText::GetEmpty());
	if (ChipNameText) ChipNameText->SetText(FText::GetEmpty());
	if (ChipFrame)
	{
		ChipFrame->SetBrush(RoundedBrush(
			FLinearColor(0.f, 0.f, 0.f, 0.f),
			FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.45f),
			1.f, 2.f));
	}

	// Empty cells: NativeOnMouseButtonDown / NativeOnMouseButtonUp short-
	// circuit on bIsEmpty so blank-space clicks don't pop up the detail
	// panel and don't try to arm a drag. Designer-authored WBPs that bind
	// ChipButton still need it disabled so the button doesn't steal focus.
	if (ChipButton) ChipButton->SetIsEnabled(false);
}

void UEclipseInventoryChipWidget::SetSelectedVisual(bool bSelected)
{
	bIsSelected = bSelected;
	ApplyChipVisual();
}

// Selection and hover both live here so leaving a hovered chip restores the
// SELECTED look rather than the resting one — two independent setters
// writing the same brush is how that gets lost.
void UEclipseInventoryChipWidget::ApplyChipVisual()
{
	using namespace EclipseUI;
	if (!ChipFrame) return;
	// Empty cells have their own dim-outline frame styling — never overwrite
	// it with the item-chip selected/unselected brush.
	if (bIsEmpty) return;

	float OutlineW      = bIsSelected ? 2.f : 1.f;
	FLinearColor Outline = bIsSelected ? LinkBlue : LinkBlueDim;
	FLinearColor Fill    = bIsSelected
		? FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.14f)
		: FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.04f);

	if (bIsHovered)
	{
		// A brighter rim and a lift in the fill — enough to say "this one"
		// without pretending to be selected.
		OutlineW = FMath::Max(OutlineW, 2.f);
		Outline  = DialogueRed;
		Fill     = FLinearColor(DialogueRed.R, DialogueRed.G, DialogueRed.B,
					  bIsSelected ? 0.18f : 0.10f);
	}
	ChipFrame->SetBrush(RoundedBrush(Fill, Outline, OutlineW, 4.f));
}

bool UEclipseInventoryChipWidget::Initialize()
{
	using namespace EclipseUI;
	const bool bSuper = Super::Initialize();

	// Build a minimal chip layout if none was authored: outer border ➜
	// vertical box ➜ icon text + name text. NO button overlay — the chip
	// widget handles its own mouse input (NativeOnMouseButtonDown arms drag,
	// NativeOnMouseButtonUp = click-without-drag = select). A button overlay
	// would consume mouse-down before DetectDrag could arm, breaking drag.
	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("ChipFrame"))))
	{
		UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ChipRoot"));
		WidgetTree->RootWidget = Root;

		ChipFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChipFrame"));
		ChipFrame->SetBrush(RoundedBrush(
			FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.04f),
			FLinearColor(LinkBlue.R, LinkBlue.G, LinkBlue.B, 0.6f),
			1.f, 4.f));
		ChipFrame->SetPadding(FMargin(4.f));
		if (UCanvasPanelSlot* S = Root->AddChildToCanvas(ChipFrame))
		{
			S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
			S->SetOffsets(FMargin(0.f));
		}

		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ChipCol"));
		ChipFrame->SetContent(Col);

		ChipIconText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChipIconText"));
		ChipIconText->SetFont(MakeRodin(28));
		ChipIconText->SetJustification(ETextJustify::Center);
		ChipIconText->SetColorAndOpacity(FSlateColor(LinkBlue));
		Col->AddChildToVerticalBox(ChipIconText);

		ChipNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChipNameText"));
		ChipNameText->SetFont(MakeRodin(10));
		ChipNameText->SetJustification(ETextJustify::Center);
		ChipNameText->SetColorAndOpacity(FSlateColor(LinkBlueDim));
		Col->AddChildToVerticalBox(ChipNameText);
	}

	// Back-compat: a designer-authored WBP may still bind a ChipButton via
	// BindWidgetOptional. Wire its click into the same selection path so
	// either layout works. Drag won't fire from a WBP-button-driven chip
	// (button consumes mouse-down) — designers using drag should build the
	// chip without a button overlay.
	if (ChipButton)
	{
		ChipButton->OnClicked.AddDynamic(this, &UEclipseInventoryChipWidget::OnChipClicked);
	}
	return bSuper;
}

void UEclipseInventoryChipWidget::OnChipClicked()
{
	UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ CLICK select '%s' slot=%d eq=%d"),
		*ItemId.ToString(), SlotIndex, bIsEquipped ? 1 : 0);
	// Cast to the chip-host interface — Owner can be either the modal
	// inventory widget or the permanent strip widget.
	if (IEclipseChipOwner* Host = Cast<IEclipseChipOwner>(Owner.Get()))
	{
		Host->SelectItem(ItemId, bIsEquipped);
	}
}

// ── Drag-drop ────────────────────────────────────────────────────────────────
// Detect a drag on left-mouse press. Slate fires NativeOnDragDetected once
// the cursor moves beyond the drag threshold.
FReply UEclipseInventoryChipWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsEmpty)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: MouseDown on EMPTY slot=%d (ignored)"), SlotIndex);
		return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: MouseDown LMB on '%s' slot=%d eq=%d → arm DetectDrag + CaptureMouse"),
			*ItemId.ToString(), SlotIndex, bIsEquipped ? 1 : 0);
		return FReply::Handled()
			.DetectDrag(TakeWidget(), EKeys::LeftMouseButton)
			.CaptureMouse(TakeWidget());
	}

	UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: MouseDown non-LMB on '%s' slot=%d (bubbled)"),
		*ItemId.ToString(), SlotIndex);
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UEclipseInventoryChipWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsEmpty) return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: MouseUp LMB on '%s' slot=%d → click-to-select (no drag was detected)"),
			*ItemId.ToString(), SlotIndex);
		OnChipClicked();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

void UEclipseInventoryChipWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (bIsEmpty) return;
	bIsHovered = true;
	ApplyChipVisual();
}

void UEclipseInventoryChipWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	bIsHovered = false;
	ApplyChipVisual();
}

void UEclipseInventoryChipWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	using namespace EclipseUI;
	if (bIsEmpty) return;

	UEclipseInventoryDragOp* Op = NewObject<UEclipseInventoryDragOp>(this);
	Op->Payload  = this;
	Op->Pivot    = EDragPivot::CenterCenter;

	// Build a lightweight visual preview that follows the cursor — a fresh
	// chip widget initialised with the same data, no click handlers. We
	// wrap it in an overlay so a strike-through line can be toggled on top
	// when the cursor is over a slot the item can't go into.
	UEclipseInventoryChipWidget* Preview =
		CreateWidget<UEclipseInventoryChipWidget>(GetOwningPlayer(), GetClass());
	if (Preview)
	{
		const FString IconStr = ChipIconText ? ChipIconText->GetText().ToString() : TEXT("?");
		const FString NameStr = ChipNameText ? ChipNameText->GetText().ToString() : ItemId.ToString();
		Preview->InitChip(Owner, ItemId, bIsEquipped, IconStr, NameStr, Tint);

		UOverlay* Wrap = WidgetTree
			? WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass())
			: NewObject<UOverlay>(this);
		if (Wrap)
		{
			Wrap->AddChildToOverlay(Preview);

			// Diagonal-ish strike bar (a thick red line) centred over the
			// chip. Collapsed until the panel detects an invalid hover.
			UBorder* Strike = WidgetTree
				? WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass())
				: NewObject<UBorder>(this);
			Strike->SetBrush(SolidBrush(FLinearColor(0.95f, 0.20f, 0.20f, 0.95f)));
			Strike->SetPadding(FMargin(0.f));
			USizeBox* StrikeSize = WidgetTree
				? WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass())
				: NewObject<USizeBox>(this);
			StrikeSize->SetHeightOverride(4.f);
			StrikeSize->AddChild(Strike);
			StrikeSize->SetVisibility(ESlateVisibility::Collapsed);
			if (UOverlaySlot* OS = Wrap->AddChildToOverlay(StrikeSize))
			{
				OS->SetHorizontalAlignment(HAlign_Fill);
				OS->SetVerticalAlignment(VAlign_Center);
			}
			Op->StrikeLine        = StrikeSize;
			Op->DefaultDragVisual = Wrap;
		}
		else
		{
			Op->DefaultDragVisual = Preview;
		}
	}

	OutOperation = Op;
	UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ DRAG START '%s' from slot=%d eq=%d (preview built=%d)"),
		*ItemId.ToString(), SlotIndex, bIsEquipped ? 1 : 0, Preview != nullptr ? 1 : 0);
}

void UEclipseInventoryChipWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Drag ended without landing on a drop-target → drop the item.
	// (NativeOnDrop on the inventory widget accepts drops inside the panel,
	// so this only fires when the chip was released outside it.)
	if (IEclipseChipOwner* Host = Cast<IEclipseChipOwner>(Owner.Get()))
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ DRAG CANCELLED outside panel — dropping '%s' from slot=%d eq=%d"),
			*ItemId.ToString(), SlotIndex, bIsEquipped ? 1 : 0);
		Host->HandleChipDroppedOutside(ItemId, bIsEquipped);
	}
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
}

bool UEclipseInventoryChipWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Returning true marks this widget as a valid drop target so Slate will
	// route NativeOnDrop here when the cursor releases over the chip. Without
	// this the drop falls through to the inventory panel's no-op handler.
	Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
	return true;
}

bool UEclipseInventoryChipWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Resolve the dragged source chip from the operation payload.
	UEclipseInventoryChipWidget* Source = InOperation
		? Cast<UEclipseInventoryChipWidget>(InOperation->Payload)
		: nullptr;

	if (!Source || !Owner || Source->Owner != Owner)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: drop on slot=%d ignored (no source / cross-widget)"), SlotIndex);
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	if (Source == this)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ DROP on SELF (slot=%d item='%s') — no-op"),
			SlotIndex, *ItemId.ToString());
		return true;
	}

	UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ DROP src='%s' (slot=%d eq=%d) → tgt slot=%d (target=%s, empty=%d)"),
		*Source->ItemId.ToString(), Source->SlotIndex, Source->bIsEquipped ? 1 : 0,
		SlotIndex, bIsEmpty ? TEXT("[empty]") : *ItemId.ToString(), bIsEmpty ? 1 : 0);

	if (IEclipseChipOwner* Host = Cast<IEclipseChipOwner>(Owner.Get()))
	{
		Host->HandleChipDroppedOnSlot(Source->ItemId, Source->bIsEquipped, SlotIndex);
	}
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Clothing slot — one drop target per body part (Head/Eyes/Neck/Top/
//  Bottom/Shoes). Sits in a horizontal strip above the chip grid; drops
//  a chip onto it to equip, drags out to unequip.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	// Pretty label for a slot — matches the enum's DisplayName meta but
	// uppercased for the UI.
	const TCHAR* SlotLabelText(EEclipseSlotType Slot)
	{
		switch (Slot)
		{
		case EEclipseSlotType::Head:   return TEXT("HEAD");
		case EEclipseSlotType::Eyes:   return TEXT("EYES");
		case EEclipseSlotType::Neck:   return TEXT("NECK");
		case EEclipseSlotType::Top:    return TEXT("TOP");
		case EEclipseSlotType::Bottom: return TEXT("BOTTOM");
		case EEclipseSlotType::Shoes:  return TEXT("SHOES");
		case EEclipseSlotType::Hands:  return TEXT("HANDS");
		case EEclipseSlotType::Pockets:return TEXT("POCKETS");
		}
		return TEXT("?");
	}

	// Compact letter badge for an equipped item — first 3 letters of its
	// display name, uppercased. Keeps the slot a "square with letters"
	// rather than an emoji symbol.
	FString ItemAbbrev(const FString& DisplayName, FName FallbackId)
	{
		FString Src = DisplayName.IsEmpty() ? FallbackId.ToString() : DisplayName;
		Src.TrimStartAndEndInline();
		Src = Src.ToUpper();
		return Src.Left(3);
	}
}

bool UEclipseClothingSlotWidget::Initialize()
{
	using namespace EclipseUI;

	// This is a C++-only UUserWidget with no compiled WBP, so its private
	// WidgetTree is null until Super::Initialize() (called at the bottom)
	// lazily creates an EMPTY one. But UUserWidget::RebuildWidget() reads
	// WidgetTree->RootWidget to produce the slate — if that's null it
	// returns an SSpacer and the slot renders invisible. So we must build
	// our own tree + RootWidget here, before Super runs.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}

	// Build a minimal C++ fallback tree if the WBP didn't ship widgets
	// for the slot — designer can replace by binding SlotLabel/SlotIcon/
	// SlotFrame in their own WBP_Slot later.
	if (!SlotFrame && !SlotLabel && !SlotIcon)
	{
		// Caption OUTSIDE the box, box below it. The label used to sit
		// inside the frame and eat half the height, which left no room for
		// the prefab render to be anything but a stamp.
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(
			UVerticalBox::StaticClass(), TEXT("SlotCol"));
		WidgetTree->RootWidget = Col;

		SlotLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotLabel"));
		SlotLabel->SetFont(MakeFragmentMono(10, 1.f));
		SlotLabel->SetColorAndOpacity(FSlateColor(DialogueRed));
		SlotLabel->SetJustification(ETextJustify::Center);
		SlotLabel->SetText(FText::FromString(SlotLabelText(SlotType)));
		if (UVerticalBoxSlot* LS = Col->AddChildToVerticalBox(SlotLabel))
		{
			LS->SetHorizontalAlignment(HAlign_Center);
			LS->SetPadding(FMargin(0.f, 0.f, 0.f, 2.f));
			LS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		}

		SlotFrame = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SlotFrame"));
		{
			FSlateBrush B;
			B.DrawAs    = ESlateBrushDrawType::RoundedBox;
			// Near-black fill and the game's stall-door red on the rim.
			B.TintColor = FSlateColor(FLinearColor(0.04f, 0.03f, 0.04f, 0.88f));
			B.OutlineSettings.Color        = FSlateColor(DialogueRed);
			B.OutlineSettings.Width        = 1.5f;
			B.OutlineSettings.CornerRadii  = FVector4(2.f, 2.f, 2.f, 2.f);
			B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			SlotFrame->SetBrush(B);
		}
		SlotFrame->SetPadding(FMargin(2.f));
		SlotFrame->SetHorizontalAlignment(HAlign_Fill);
		SlotFrame->SetVerticalAlignment(VAlign_Fill);
		if (UVerticalBoxSlot* FS = Col->AddChildToVerticalBox(SlotFrame))
		{
			FS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}

		// The badge and the prefab render share one cell — an item with a
		// baked IconTexture shows the model, anything else falls back to
		// letters. Both are built either way so RefreshFromState only has
		// to flip visibility.
		UOverlay* IconCell = WidgetTree->ConstructWidget<UOverlay>(
			UOverlay::StaticClass(), TEXT("SlotIconCell"));
		SlotFrame->SetContent(IconCell);

		SlotIcon = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotIcon"));
		// Sized for a 3-letter badge ("JKT", "SHO", …) inside the square.
		SlotIcon->SetFont(MakeFragmentMono(18));
		SlotIcon->SetColorAndOpacity(FSlateColor(DialogueRed.CopyWithNewOpacity(0.35f)));
		SlotIcon->SetJustification(ETextJustify::Center);
		SlotIcon->SetText(FText::GetEmpty());
		if (UOverlaySlot* IS = IconCell->AddChildToOverlay(SlotIcon))
		{
			IS->SetHorizontalAlignment(HAlign_Center);
			IS->SetVerticalAlignment(VAlign_Center);
		}

		// The renders are square and the slot box is wide, so a plain Fill
		// would stretch the model sideways. MaxAspectRatio 1 keeps it
		// square while still growing to the full height of the box.
		USizeBox* ThumbBox = WidgetTree->ConstructWidget<USizeBox>(
			USizeBox::StaticClass(), TEXT("SlotThumbBox"));
		ThumbBox->SetMaxAspectRatio(1.f);

		SlotThumb = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SlotThumb"));
		SlotThumb->SetVisibility(ESlateVisibility::Collapsed);
		ThumbBox->AddChild(SlotThumb);
		if (UOverlaySlot* TS = IconCell->AddChildToOverlay(ThumbBox))
		{
			// Fill both ways and let MaxAspectRatio do the centring —
			// HAlign_Center would hand the box the image's 32px default
			// brush size instead of the room it actually has.
			TS->SetHorizontalAlignment(HAlign_Fill);
			TS->SetVerticalAlignment(VAlign_Fill);
		}
	}

	// Even when the WBP has the bindings, refresh the label text from
	// SlotType so the populator can build all six with the same widget
	// class and the right caption shows up at runtime.
	if (SlotLabel)
	{
		SlotLabel->SetText(FText::FromString(SlotLabelText(SlotType)));
	}

	return Super::Initialize();
}

bool UEclipseClothingSlotWidget::IsCarrier() const
{
	return SlotType == EEclipseSlotType::Hands || SlotType == EEclipseSlotType::Pockets;
}

FName UEclipseClothingSlotWidget::GetOccupant() const
{
	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return NAME_None;

	if (!IsCarrier()) return GS->GetEquippedInSlot(SlotType);

	// Carriers hold several items; this widget draws exactly one of them.
	const TArray<FName> Held = GS->GetItemsInSlot(SlotType);
	return Held.IsValidIndex(CellIndex) ? Held[CellIndex] : NAME_None;
}

void UEclipseClothingSlotWidget::RefreshFromState()
{
	using namespace EclipseUI;

	// The label is also re-applied here (not just in Initialize) because
	// the slot's SlotType is assigned by the inventory AFTER CreateWidget
	// has already run Initialize with the default type — so without this
	// every slot would keep showing "HEAD".
	if (SlotLabel)
	{
		SlotLabel->SetText(FText::FromString(SlotLabelText(SlotType)));
	}

	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	const FName Occupant = GetOccupant();
	if (Occupant.IsNone())
	{
		// Empty slots show a "+": the caption above already names the slot,
		// so the old 3-letter codes just said it twice in a worse typeface,
		// but a bare box gave no hint it was a drop target at all.
		if (SlotThumb) SlotThumb->SetVisibility(ESlateVisibility::Collapsed);
		if (SlotIcon)
		{
			SlotIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			SlotIcon->SetText(FText::FromString(TEXT("+")));
			SlotIcon->SetFont(MakeBMSPA(22));
			SlotIcon->SetColorAndOpacity(FSlateColor(DialogueRed.CopyWithNewOpacity(0.45f)));
		}
	}
	else
	{
		// Occupied: show a letter badge (3-letter abbrev of the item's
		// name) in the item's tint colour — a "square with letters",
		// not an emoji symbol. Carriers hold ordinary items, so fall back
		// to DT_Items when the id isn't a garment.
		FString Label;
		FLinearColor Tint = LinkBlue;
		TSoftObjectPtr<UTexture2D> Thumb;

		FEclipseClothingRow CRow;
		FEclipseItemRow     IRow;
		if (GS->GetClothingRow(Occupant, CRow))
		{
			Label = CRow.DisplayName.ToString();
			Tint  = CRow.TintColor;
			Thumb = CRow.IconTexture;
		}
		else if (GS->GetItemRow(Occupant, IRow))
		{
			Label = IRow.DisplayName.ToString();
			Tint  = IRow.TintColor;
			Thumb = IRow.IconTexture;
		}

		// A stack shows its count rather than a name or a picture —
		// cigarettes are the only one, and "CIG" tells you nothing you can
		// act on when what matters is whether you have twenty.
		const bool bIsStack = UEclipseGameStateSubsystem::GetBaseItemId(Occupant)
			== UEclipseGameStateSubsystem::CigaretteItemId;
		if (bIsStack)
		{
			Label = FString::Printf(TEXT("%d"), GS->Cigarettes);
		}

		// Synchronous: the panel is modal and the texture is a few hundred
		// KB, so there's nothing to gain from streaming it in a frame late.
		UTexture2D* ThumbTex = Thumb.IsNull() ? nullptr : Thumb.LoadSynchronous();
		if (SlotThumb)
		{
			SlotThumb->SetVisibility(ThumbTex
				? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			if (ThumbTex)
			{
				SlotThumb->SetBrushFromTexture(ThumbTex, /*bMatchSize=*/false);
			}
		}
		if (SlotIcon)
		{
			// A stack keeps its count ON TOP of the picture — "20" in the
			// corner of a photo of cigarettes, not one instead of the
			// other. Anything else falls back to letters only when it has
			// no baked render to show.
			const bool bWantText = bIsStack || !ThumbTex;
			SlotIcon->SetVisibility(bWantText
				? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
			SlotIcon->SetFont(MakeFragmentMono(bIsStack ? 14 : 18));
			SlotIcon->SetText(FText::FromString(
				bIsStack ? Label : ItemAbbrev(Label, Occupant)));
			SlotIcon->SetColorAndOpacity(FSlateColor(bIsStack ? Cream : Tint));

			// Bottom-right when it's a badge over an image, centred when
			// it's standing in for one.
			if (UOverlaySlot* OS = Cast<UOverlaySlot>(SlotIcon->Slot))
			{
				const bool bBadge = bIsStack && ThumbTex;
				OS->SetHorizontalAlignment(bBadge ? HAlign_Right  : HAlign_Center);
				OS->SetVerticalAlignment  (bBadge ? VAlign_Bottom : VAlign_Center);
				OS->SetPadding(bBadge ? FMargin(0.f, 0.f, 3.f, 1.f) : FMargin(0.f));
			}
		}
	}
}

void UEclipseClothingSlotWidget::NativeOnMouseEnter(const FGeometry& G, const FPointerEvent& E)
{
	Super::NativeOnMouseEnter(G, E);
	bSlotHovered = true;
	if (UseFlashAlpha <= 0.f) SetHoverFeedback(EHoverState::Idle);
}

void UEclipseClothingSlotWidget::NativeOnMouseLeave(const FPointerEvent& E)
{
	Super::NativeOnMouseLeave(E);
	bSlotHovered = false;
	if (UseFlashAlpha <= 0.f) SetHoverFeedback(EHoverState::Idle);
}

void UEclipseClothingSlotWidget::FlashUse()
{
	UseFlashAlpha = 1.f;
}

void UEclipseClothingSlotWidget::NativeTick(const FGeometry& G, float DeltaTime)
{
	Super::NativeTick(G, DeltaTime);
	using namespace EclipseUI;

	if (UseFlashAlpha <= 0.f || !SlotFrame) return;

	// ~0.9s decay, eased. Drives the frame, the thumbnail AND the count
	// together so the whole tile lights up and settles, rather than a rim
	// flashing round a picture that never reacted. The curve is squared so
	// it holds near full brightness before falling away — a linear ramp at
	// this length just looked like a slow dim.
	UseFlashAlpha = FMath::Max(0.f, UseFlashAlpha - DeltaTime / UseFlashSeconds);
	const float K = FMath::Sqrt(UseFlashAlpha);

	FSlateBrush B;
	B.DrawAs = ESlateBrushDrawType::RoundedBox;
	B.TintColor = FSlateColor(FLinearColor(
		FMath::Lerp(0.04f, 1.0f, K), FMath::Lerp(0.03f, 0.92f, K),
		FMath::Lerp(0.04f, 0.86f, K), 0.88f));
	B.OutlineSettings.Color        = FSlateColor(FLinearColor::LerpUsingHSV(DialogueRed, FLinearColor::White, K));
	B.OutlineSettings.Width        = FMath::Lerp(1.5f, 6.f, K);
	B.OutlineSettings.CornerRadii  = FVector4(2.f, 2.f, 2.f, 2.f);
	B.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	SlotFrame->SetBrush(B);

	const float Glow = 1.f + K * 4.f;   // >1 blows past white into a bloom
	if (SlotThumb)
	{
		SlotThumb->SetColorAndOpacity(FLinearColor(Glow, Glow, Glow, 1.f));
	}
	// The quantity glows too — on a stack the number IS the item, and
	// leaving it flat while everything around it flared looked broken.
	if (SlotIcon)
	{
		SlotIcon->SetColorAndOpacity(FSlateColor(FLinearColor(Glow, Glow, Glow, 1.f)));
	}
	if (UseFlashAlpha <= 0.f)
	{
		SetHoverFeedback(EHoverState::Idle);
		if (SlotThumb) SlotThumb->SetColorAndOpacity(FLinearColor::White);
		RefreshFromState();          // restores the icon's own colour
	}
}

void UEclipseClothingSlotWidget::SetHoverFeedback(EHoverState State)
{
	using namespace EclipseUI;
	if (!SlotFrame) return;

	// Re-tint the slot frame to signal whether the dragged item belongs
	// here. Invalid = greyed out (the "no" state the design asked for).
	FSlateBrush B;
	B.DrawAs                        = ESlateBrushDrawType::RoundedBox;
	B.OutlineSettings.CornerRadii   = FVector4(2.f, 2.f, 2.f, 2.f);
	B.OutlineSettings.RoundingType  = ESlateBrushRoundingType::FixedRadius;

	const FLinearColor Green(0.36f, 0.85f, 0.45f, 1.f);
	const FLinearColor Grey (0.45f, 0.45f, 0.48f, 1.f);

	switch (State)
	{
	case EHoverState::Valid:
		B.TintColor             = FSlateColor(FLinearColor(0.10f, 0.22f, 0.13f, 0.95f));
		B.OutlineSettings.Color = FSlateColor(Green);
		B.OutlineSettings.Width = 2.f;
		break;
	case EHoverState::Invalid:
		B.TintColor             = FSlateColor(FLinearColor(0.10f, 0.10f, 0.11f, 0.92f));
		B.OutlineSettings.Color = FSlateColor(Grey);
		B.OutlineSettings.Width = 1.5f;
		break;
	case EHoverState::Idle:
	default:
		// Hovering a slot lifts its fill and brightens the rim — the same
		// "this one" cue the chips give, so an occupied slot reads as
		// something you can pick up rather than a label.
		B.TintColor             = FSlateColor(bSlotHovered
			? FLinearColor(0.16f, 0.05f, 0.06f, 0.94f)
			: FLinearColor(0.04f, 0.03f, 0.04f, 0.88f));
		B.OutlineSettings.Color = FSlateColor(bSlotHovered
			? FLinearColor::LerpUsingHSV(DialogueRed, FLinearColor::White, 0.45f)
			: DialogueRed);
		B.OutlineSettings.Width = bSlotHovered ? 2.5f : 1.5f;
		break;
	}
	SlotFrame->SetBrush(B);

	// Dim the label + icon when greyed out so the whole tile reads dead.
	const float ContentAlpha = (State == EHoverState::Invalid) ? 0.35f : 1.f;
	if (SlotLabel)
	{
		const FLinearColor LabelCol = (State == EHoverState::Invalid)
			? FLinearColor(Grey.R, Grey.G, Grey.B, ContentAlpha)
			: (State == EHoverState::Valid ? Green : DialogueRed);
		SlotLabel->SetColorAndOpacity(FSlateColor(LabelCol));
	}
}

FReply UEclipseClothingSlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const FName Occupant = GetOccupant();
	if (Occupant.IsNone() || !OwningInventory)
	{
		return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	}

	// Carriers hold loose items; body slots hold worn clothing. The flag
	// drives which table the detail panel reads and what DROP does.
	OwningInventory->SelectItem(Occupant, /*bIsClothing=*/!IsCarrier());
	return FReply::Handled();
}

bool UEclipseClothingSlotWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

	// Live feedback while a chip hovers over this slot: green if the item
	// belongs here, greyed-out + strike-through on the dragged icon if not.
	UEclipseInventoryChipWidget* Source = InOperation
		? Cast<UEclipseInventoryChipWidget>(InOperation->Payload) : nullptr;
	if (Source)
	{
		UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;

		bool bValid = false;
		if (GS)
		{
			if (IsCarrier())
			{
				// Carriers take anything that fits — the only rejections are
				// "full" and "too big for a pocket".
				bValid = GS->CanPlaceInSlot(Source->GetItemId(), SlotType);
			}
			else
			{
				FEclipseClothingRow Row;
				bValid = GS->GetClothingRow(Source->GetItemId(), Row) && Row.SlotType == SlotType;
			}
		}

		SetHoverFeedback(bValid ? EHoverState::Valid : EHoverState::Invalid);

		if (UEclipseInventoryDragOp* Op = Cast<UEclipseInventoryDragOp>(InOperation))
		{
			if (Op->StrikeLine)
			{
				Op->StrikeLine->SetVisibility(bValid
					? ESlateVisibility::Collapsed
					: ESlateVisibility::HitTestInvisible);
			}
		}
	}

	// Always accept drag-over so Slate routes the drop to us. The actual
	// "is this clothing of the right slot" check happens in NativeOnDrop.
	return true;
}

void UEclipseClothingSlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Cursor left this slot — clear its feedback and the strike line.
	SetHoverFeedback(EHoverState::Idle);
	if (UEclipseInventoryDragOp* Op = Cast<UEclipseInventoryDragOp>(InOperation))
	{
		if (Op->StrikeLine) Op->StrikeLine->SetVisibility(ESlateVisibility::Collapsed);
	}
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
}

bool UEclipseClothingSlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Drop landed — clear hover tint regardless of accept/reject outcome.
	SetHoverFeedback(EHoverState::Idle);

	UEclipseInventoryChipWidget* Source = InOperation
		? Cast<UEclipseInventoryChipWidget>(InOperation->Payload) : nullptr;
	if (!Source || !OwningInventory)
	{
		return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);
	}

	const FName ItemId = Source->GetItemId();

	// Verify the dropped chip is clothing of this slot's type. Reject
	// drops of consumables, or of clothing for a different body part.
	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

	// Carrier: move the item into Hands / Pockets. If it was worn, take it
	// off first — a jacket dragged from the body into a pocket should leave
	// the body slot empty rather than exist in both places.
	if (IsCarrier())
	{
		if (!GS->CanPlaceInSlot(ItemId, SlotType))
		{
			UE_LOG(LogEclipse, Log, TEXT("Slot[%s]: drop of '%s' rejected — full, or too large to pocket"),
				SlotLabelText(SlotType), *ItemId.ToString());
			return true;
		}
		GS->MoveItemToCarrier(ItemId, SlotType);
		return true;
	}

	FEclipseClothingRow Row;
	if (!GS->GetClothingRow(ItemId, Row))
	{
		UE_LOG(LogEclipse, Log,
			TEXT("Slot[%s]: drop of '%s' rejected — not in DT_Clothing"),
			SlotLabelText(SlotType), *ItemId.ToString());
		return true;   // consume the drop so it doesn't fall through
	}
	if (Row.SlotType != SlotType)
	{
		UE_LOG(LogEclipse, Log,
			TEXT("Slot[%s]: drop of '%s' rejected — wrong slot type (%d vs %d)"),
			SlotLabelText(SlotType), *ItemId.ToString(),
			(int32)Row.SlotType, (int32)SlotType);
		return true;
	}

	OwningInventory->EquipChipToSlot(ItemId, SlotType);
	return true;
}

void UEclipseClothingSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS || !OwningInventory) return;

	const FName Equipped = GetOccupant();
	if (Equipped.IsNone()) return;   // nothing to drag out of an empty slot

	// Build a drag op carrying the item id so the drop handlers can route
	// it. The existing drop path expects a UEclipseInventoryChipWidget as
	// Payload, so synthesise a transient one as visual + payload.
	UEclipseInventoryChipWidget* Stand =
		CreateWidget<UEclipseInventoryChipWidget>(GetOwningPlayer(), UEclipseInventoryChipWidget::StaticClass());
	if (Stand)
	{
		// Look in both tables — a carrier cell can hold a plain item.
		FString Icon, Name;
		FLinearColor Tint = FLinearColor::White;
		FEclipseClothingRow CRow;
		FEclipseItemRow     IRow;
		if (GS->GetClothingRow(Equipped, CRow))
		{
			Icon = CRow.Icon; Name = CRow.DisplayName.ToString(); Tint = CRow.TintColor;
		}
		else if (GS->GetItemRow(Equipped, IRow))
		{
			Icon = IRow.Icon; Name = IRow.DisplayName.ToString(); Tint = IRow.TintColor;
		}
		if (Icon.IsEmpty()) Icon = Equipped.ToString().Left(1);
		if (Name.IsEmpty()) Name = Equipped.ToString();

		Stand->InitChip(OwningInventory, Equipped, /*bEquipped=*/!IsCarrier(), Icon, Name, Tint);
	}

	// On drag-cancel (drop outside any target), unequip the slot — the
	// item goes back to the inventory grid via UnequipFromSlot anyway,
	// but firing here makes the visual feedback immediate.
	UDragDropOperation* Op = NewObject<UDragDropOperation>(this);
	Op->Payload          = Stand;   // chip widget for compatibility
	Op->DefaultDragVisual = Stand;
	Op->Pivot            = EDragPivot::CenterCenter;
	OutOperation = Op;

	// Optimistically unequip — if the drop lands somewhere invalid the
	// chip is still in the inventory, which is the correct outcome.
	OwningInventory->UnequipFromSlot(SlotType);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory overlay — tabs + 6×3 grid + detail + action bar.
//  Click a chip → it becomes selected; the bottom panel renders its row data
//  (DisplayName, Description) and the action buttons operate on it.
// ─────────────────────────────────────────────────────────────────────────────

UEclipseInventoryWidget* UEclipseInventoryWidget::OpenForPlayer(APlayerController* PC)
{
	if (!PC) return nullptr;

	// Prefer designer-styled WBP_Inventory; fall back to the C++ fallback tree.
	TSubclassOf<UEclipseInventoryWidget> Cls = UEclipseInventoryWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseInventoryWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_Inventory.WBP_Inventory_C")))
	{
		Cls = BPClass;
	}

	UEclipseInventoryWidget* W = CreateWidget<UEclipseInventoryWidget>(PC, Cls, TEXT("Inventory"));
	if (!W) return nullptr;

	W->AddToViewport(/*ZOrder=*/100);
	W->SetIsFocusable(true);
	W->SetKeyboardFocus();

	// Diagnostic: dump tree size + visibility right after mount so we can
	// tell whether the fallback build produced a populated hierarchy or
	// an empty root that would render as nothing.
	if (W->WidgetTree)
	{
		TArray<UWidget*> All;
		W->WidgetTree->GetAllWidgets(All);
		UE_LOG(LogEclipse, Log, TEXT("Inventory: tree has %d widgets, root=%s, visibility=%d, desired=%s"),
			All.Num(),
			W->WidgetTree->RootWidget ? *W->WidgetTree->RootWidget->GetName() : TEXT("none"),
			(int32)W->GetVisibility(),
			*W->GetDesiredSize().ToString());
	}

	// Pause world while inventory's open. UI mode + cursor on so the player
	// can click chips / buttons without moving the camera around.
	UGameplayStatics::SetGamePaused(W->GetWorld(), true);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);

	if (UEclipseAudioSubsystem* A = PC->GetGameInstance()
			? PC->GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		A->PlayCue(EEclipseUiCue::MenuOpen);
	}
	UE_LOG(LogEclipse, Log, TEXT("Inventory: opened for %s"), *PC->GetName());
	return W;
}

void UEclipseInventoryWidget::Close()
{
	APlayerController* PC = GetOwningPlayer();
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGamePaused(W, false);
	}
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
	RemoveFromParent();
}

bool UEclipseInventoryWidget::Initialize()
{
	UE_LOG(LogEclipse, Log, TEXT("Inventory::Initialize — WidgetTree=%s"),
		WidgetTree ? TEXT("set") : TEXT("null"));

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("HeldGrid"))))
	{
		UE_LOG(LogEclipse, Log, TEXT("Inventory::Initialize — building fallback tree"));
		BuildFallbackTree();
		UE_LOG(LogEclipse, Log, TEXT("Inventory::Initialize — root after build: %s"),
			(WidgetTree->RootWidget) ? *WidgetTree->RootWidget->GetName() : TEXT("STILL NONE"));
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("Inventory::Initialize — fallback skipped (HeldGrid present, or tree null)"));
	}
	return Super::Initialize();
}

void UEclipseInventoryWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Full-screen dim
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.65f)));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	// Centred paper panel — white ground, hyperlink-blue rule.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryPanel"));
	Panel->SetBrush(RoundedBrush(PaperWhite, LinkBlue, 1.f, 8.f));
	Panel->SetPadding(FMargin(36.f, 28.f));
	Panel->SetHorizontalAlignment(HAlign_Fill);
	Panel->SetVerticalAlignment(VAlign_Fill);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(820.f, 540.f));
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("InventoryColumn"));
	Panel->SetContent(Column);

	// Title
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InventoryTitle"));
	Title->SetText(FText::FromString(TEXT("INVENTORY")));
	Title->SetFont(MakeRodin(48));
	Title->SetColorAndOpacity(FSlateColor(LinkBlue));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	// Two-column layout: HELD (left) | EQUIPPED (right)
	UHorizontalBox* TopRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopRow"));
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(TopRow))
	{
		VS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		VS->SetHorizontalAlignment(HAlign_Fill);
	}

	// Helper for column header
	auto MakeColumnHeader = [&](const FString& Label) -> UTextBlock*
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(Label));
		T->SetFont(MakeRodin(16));
		T->SetColorAndOpacity(FSlateColor(LinkBlue));
		return T;
	};

	// ── Held column ──
	UVerticalBox* HeldColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("HeldColumn"));
	if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(HeldColumn))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f));
	}
	if (UVerticalBoxSlot* VS = HeldColumn->AddChildToVerticalBox(MakeColumnHeader(TEXT("HELD"))))
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	HeldGrid = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeldGrid"));
	HeldColumn->AddChildToVerticalBox(HeldGrid);

	// ── Equipped column ──
	UVerticalBox* EquippedHolder = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EquippedHolder"));
	if (UHorizontalBoxSlot* HS = TopRow->AddChildToHorizontalBox(EquippedHolder))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (UVerticalBoxSlot* VS = EquippedHolder->AddChildToVerticalBox(MakeColumnHeader(TEXT("EQUIPPED"))))
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

	EquippedColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("EquippedColumn"));
	EquippedHolder->AddChildToVerticalBox(EquippedColumn);

	// ── Detail / action panel (bottom) ──
	UVerticalBox* DetailPanel = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DetailPanel"));
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(DetailPanel))
	{
		VS->SetPadding(FMargin(0.f, 28.f, 0.f, 0.f));
	}

	SelectedNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedNameText"));
	SelectedNameText->SetText(FText::FromString(TEXT("(select an item)")));
	SelectedNameText->SetFont(MakeRodin(20));
	SelectedNameText->SetColorAndOpacity(FSlateColor(LinkBlue));
	if (UVerticalBoxSlot* VS = DetailPanel->AddChildToVerticalBox(SelectedNameText))
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	SelectedDescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedDescText"));
	SelectedDescText->SetText(FText::FromString(TEXT("Click something on the body to inspect it.")));
	SelectedDescText->SetColorAndOpacity(FSlateColor(LinkBlueDim));
	SelectedDescText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* VS = DetailPanel->AddChildToVerticalBox(SelectedDescText))
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 18.f));

	// Action button row
	UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ActionRow"));
	DetailPanel->AddChildToVerticalBox(Actions);

	auto MakeBtn = [&](const FString& Label, FName WidgetName) -> UButton*
	{
		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), WidgetName);
		FButtonStyle BS;
		// Identical in all three states so the hover can CROSS-FADE via the
		// button's BackgroundColor tint instead of snapping between brushes.
		BS.Normal   = SolidBrush(FLinearColor(DialogueRed.R, DialogueRed.G, DialogueRed.B, 0.26f));
		BS.Hovered  = BS.Normal;
		BS.Pressed  = SolidBrush(FLinearColor(DialogueRed.R, DialogueRed.G, DialogueRed.B, 0.44f));
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(FText::FromString(Label));
		T->SetColorAndOpacity(FSlateColor(LinkBlue));
		T->SetJustification(ETextJustify::Center);
		Btn->SetContent(T);

		if (UHorizontalBoxSlot* HS = Actions->AddChildToHorizontalBox(Btn))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(4.f, 8.f));
		}
		return Btn;
	};

	UseBtn    = MakeBtn(TEXT("USE"),    TEXT("UseBtn"));
	CloseBtn  = MakeBtn(TEXT("CLOSE"),  TEXT("CloseBtn"));
}

void UEclipseInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UseBtn)         UseBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnUse);
	if (CloseBtn)       CloseBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnCloseClicked);

	UButton* AllBtns[] = { UseBtn, CloseBtn };
	for (UButton* B : AllBtns) if (B) B->SetClickMethod(EButtonClickMethod::MouseDown);

	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseInventoryWidget::HandleStateChanged);
	}

	// ── Wearable slot drop targets ─────────────────────────────────────
	//
	// Build the 6 slots and slot them into a horizontal row inside the
	// inventory panel if the WBP didn't already ship them. Each slot
	// stores its SlotType + a back-pointer so its NativeOnDrop can call
	// back into EquipChipToSlot.
	auto SetupOrBuildSlot = [&](TObjectPtr<UEclipseClothingSlotWidget>& Slot,
	                             EEclipseSlotType SlotType,
	                             const TCHAR* Name,
	                             int32 CellIndex = 0) -> UEclipseClothingSlotWidget*
	{
		if (!WidgetTree) return nullptr;

		// Always create the slot at RUNTIME via CreateWidget — this fully
		// initialises the UserWidget (builds its WidgetTree + RootWidget),
		// exactly like the item-grid chips. A slot embedded by the
		// populator (ConstructWidget) renders as an invisible SSpacer
		// because its RootWidget is null, so we never reuse a bound one.
		UEclipseClothingSlotWidget* W = CreateWidget<UEclipseClothingSlotWidget>(
			this, UEclipseClothingSlotWidget::StaticClass());
		if (!W) return nullptr;
		W->SlotType = SlotType;
		W->CellIndex = CellIndex;
		W->OwningInventory = this;
		// UUserWidget defaults to SelfHitTestInvisible, which means the
		// slot itself never captures drag-over / drop events (they pass
		// straight through to the panel behind it). Force Visible so the
		// slot's NativeOnDrop fires when a chip is dropped on it.
		W->SetVisibility(ESlateVisibility::Visible);

		// Insert into the populator-built mount placeholder at the right
		// canvas position ("<Name>Mount"). The mount is a UBorder, so
		// SetContent swaps our freshly-built slot in. Mount itself is made
		// SelfHitTestInvisible so only the slot (its child) is the drop
		// target — the border background never intercepts.
		const FString MountName = FString::Printf(TEXT("%sMount"), Name);
		if (UBorder* Mount = Cast<UBorder>(WidgetTree->FindWidget(FName(*MountName))))
		{
			Mount->SetContent(W);
			Mount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}

		// Re-apply label + placeholder now that SlotType is set (Initialize
		// ran during CreateWidget with the default type).
		W->RefreshFromState();

		Slot = W;
		return W;
	};
	SetupOrBuildSlot(HeadSlot,   EEclipseSlotType::Head,   TEXT("HeadSlot"));
	SetupOrBuildSlot(EyesSlot,   EEclipseSlotType::Eyes,   TEXT("EyesSlot"));
	SetupOrBuildSlot(NeckSlot,   EEclipseSlotType::Neck,   TEXT("NeckSlot"));
	SetupOrBuildSlot(TopSlot,    EEclipseSlotType::Top,    TEXT("TopSlot"));
	SetupOrBuildSlot(BottomSlot, EEclipseSlotType::Bottom, TEXT("BottomSlot"));
	SetupOrBuildSlot(ShoesSlot,  EEclipseSlotType::Shoes,  TEXT("ShoesSlot"));
	// Carriers. Both pocket cells share SlotType=Pockets and differ only by
	// CellIndex, which is what lets one 2-capacity carrier draw as two cells.
	SetupOrBuildSlot(HandsSlot,   EEclipseSlotType::Hands,   TEXT("HandsSlot"));
	SetupOrBuildSlot(Pocket0Slot, EEclipseSlotType::Pockets, TEXT("Pocket0Slot"), 0);
	SetupOrBuildSlot(Pocket1Slot, EEclipseSlotType::Pockets, TEXT("Pocket1Slot"), 1);

	// If none of the 6 slots had a parent in the WBP, build a horizontal
	// strip and add them all to the InventoryPanel above the chip grid.
	// (When the populator runs, the slots will already be parented and
	// this branch is a no-op.)
	if (HeadSlot && !HeadSlot->GetParent() && WidgetTree)
	{
		// Find the InventoryPanel column the populator builds (or fall
		// back to RootWidget if missing). Insert a horizontal box at the
		// top with the 6 slots inside.
		UPanelWidget* TopColumn = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("InventoryColumn")));
		if (!TopColumn) TopColumn = Cast<UPanelWidget>(WidgetTree->RootWidget);
		if (TopColumn)
		{
			UHorizontalBox* SlotRow = WidgetTree->ConstructWidget<UHorizontalBox>(
				UHorizontalBox::StaticClass(), TEXT("SlotRow"));
			UEclipseClothingSlotWidget* All[] = {
				HeadSlot, EyesSlot, NeckSlot, TopSlot, BottomSlot, ShoesSlot,
				HandsSlot, Pocket0Slot, Pocket1Slot };
			for (UEclipseClothingSlotWidget* S : All)
			{
				if (!S) continue;
				if (UHorizontalBoxSlot* HS = SlotRow->AddChildToHorizontalBox(S))
				{
					HS->SetPadding(FMargin(0.f, 0.f, 6.f, 0.f));
				}
			}
			// Slot it at the top of the inventory's main column. If
			// TopColumn is a vertical box, AddChild + ShiftChild to 0.
			TopColumn->AddChild(SlotRow);
			TopColumn->ShiftChild(0, SlotRow);
		}
	}

	// Make drag essentially "instant on first cursor move". Default Slate
	// threshold is 5px which feels laggy for inventory chips. The setting
	// is global to FSlateApplication; we restore it in NativeDestruct.
	if (FSlateApplication::IsInitialized())
	{
		SavedDragTriggerDistance = FSlateApplication::Get().GetDragTriggerDistance();
		FSlateApplication::Get().SetDragTriggerDistance(0.f);
	}

	// A load that happened before the pawn existed can leave items carried
	// but unplaced, with the overflow still undropped. Opening the panel is
	// the natural moment to settle that — there's definitely a pawn now.
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->MigrateCarryPlacements();
	}

	Rebuild();
	RefreshDetailPanel();
}

void UEclipseInventoryWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnStateChanged.RemoveDynamic(this, &UEclipseInventoryWidget::HandleStateChanged);
		}
	}

	// Restore the global drag-trigger distance so other UI keeps its normal
	// click-vs-drag behaviour when the inventory closes.
	if (SavedDragTriggerDistance >= 0.f && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetDragTriggerDistance(SavedDragTriggerDistance);
		SavedDragTriggerDistance = -1.f;
	}

	Super::NativeDestruct();
}

FReply UEclipseInventoryWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	if (K == EKeys::I || K == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UEclipseInventoryWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	// Accept any drop that lands on the inventory widget itself (which
	// includes the dim layer + the chalk panel). Returning true tells
	// Slate the drop "succeeded" so NativeOnDragCancelled does NOT fire
	// on the chip — i.e. dragging within the panel is a no-op (chip
	// returns to its slot). Drops outside the entire widget hit nothing,
	// so DragCancelled fires on the chip and routes through to drop-the-
	// item-from-inventory.
	UEclipseInventoryChipWidget* Source = InOperation
		? Cast<UEclipseInventoryChipWidget>(InOperation->Payload) : nullptr;

	// Drag is ending — clear any hover feedback regardless of outcome.
	ResetSlotHovers();

	// ── Clothing-slot resolve by geometry ──────────────────────────────
	// Slate's per-widget drop routing doesn't reliably deliver the drop to
	// the canvas-positioned clothing slots (the drag-decorator layer sits
	// over them, so the hit-test bubbles past to this panel). Instead we
	// resolve the target slot here from the drop's screen point.
	if (Source)
	{
		const FVector2D ScreenPos = InDragDropEvent.GetScreenSpacePosition();
		if (UEclipseClothingSlotWidget* Slot = SlotUnderPoint(ScreenPos))
		{
			UEclipseGameStateSubsystem* GS = GetGameInstance()
				? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
			const FName ItemId = Source->GetItemId();
			FEclipseClothingRow Row;
			if (GS && GS->GetClothingRow(ItemId, Row) && Row.SlotType == Slot->SlotType)
			{
				UE_LOG(LogEclipse, Log, TEXT("Inv[panel]: drop '%s' → slot %s (equip)"),
					*ItemId.ToString(), *Slot->GetName());
				EquipChipToSlot(ItemId, Slot->SlotType);
			}
			else
			{
				UE_LOG(LogEclipse, Log,
					TEXT("Inv[panel]: drop '%s' on slot %s rejected — wrong body part"),
					*ItemId.ToString(), *Slot->GetName());
			}
			return true;   // consumed (correct slot or rejected) — no drop-to-world
		}
	}

	UE_LOG(LogEclipse, Log, TEXT("Inv[panel]: drop on panel background — no-op (src='%s' slot=%d)"),
		Source ? *Source->GetItemId().ToString() : TEXT("?"),
		Source ? Source->GetSlotIndex() : -1);
	return true;
}

// Resolve which clothing slot the screen point is over, via each slot's
// cached (absolute-space) geometry. Returns nullptr if over none.
UEclipseClothingSlotWidget* UEclipseInventoryWidget::SlotUnderPoint(const FVector2D& ScreenPos) const
{
	UEclipseClothingSlotWidget* Slots[] = {
		HeadSlot, EyesSlot, NeckSlot, TopSlot, BottomSlot, ShoesSlot };
	for (UEclipseClothingSlotWidget* S : Slots)
	{
		if (S && S->GetCachedGeometry().IsUnderLocation(ScreenPos))
		{
			return S;
		}
	}
	return nullptr;
}

void UEclipseInventoryWidget::ResetSlotHovers()
{
	UEclipseClothingSlotWidget* Slots[] = {
		HeadSlot, EyesSlot, NeckSlot, TopSlot, BottomSlot, ShoesSlot };
	for (UEclipseClothingSlotWidget* S : Slots)
	{
		if (S) S->SetHoverFeedback(UEclipseClothingSlotWidget::EHoverState::Idle);
	}
}

bool UEclipseInventoryWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

	UEclipseInventoryChipWidget* Source = InOperation
		? Cast<UEclipseInventoryChipWidget>(InOperation->Payload) : nullptr;
	if (!Source) return true;

	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;

	const FVector2D ScreenPos = InDragDropEvent.GetScreenSpacePosition();
	UEclipseClothingSlotWidget* Hovered = SlotUnderPoint(ScreenPos);

	// Is this item allowed in the hovered slot?
	bool bValid = false;
	if (Hovered && GS)
	{
		FEclipseClothingRow Row;
		bValid = GS->GetClothingRow(Source->GetItemId(), Row)
			&& Row.SlotType == Hovered->SlotType;
	}

	// Repaint every slot: hovered one gets Valid/Invalid, the rest reset.
	UEclipseClothingSlotWidget* Slots[] = {
		HeadSlot, EyesSlot, NeckSlot, TopSlot, BottomSlot, ShoesSlot };
	for (UEclipseClothingSlotWidget* S : Slots)
	{
		if (!S) continue;
		if (S == Hovered)
		{
			S->SetHoverFeedback(bValid
				? UEclipseClothingSlotWidget::EHoverState::Valid
				: UEclipseClothingSlotWidget::EHoverState::Invalid);
		}
		else
		{
			S->SetHoverFeedback(UEclipseClothingSlotWidget::EHoverState::Idle);
		}
	}

	// Strike-through on the dragged icon: shown only over an invalid slot.
	if (UEclipseInventoryDragOp* Op = Cast<UEclipseInventoryDragOp>(InOperation))
	{
		if (Op->StrikeLine)
		{
			const bool bStrike = (Hovered != nullptr) && !bValid;
			Op->StrikeLine->SetVisibility(bStrike
				? ESlateVisibility::HitTestInvisible
				: ESlateVisibility::Collapsed);
		}
	}

	return true;
}

void UEclipseInventoryWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	ResetSlotHovers();
	if (UEclipseInventoryDragOp* Op = Cast<UEclipseInventoryDragOp>(InOperation))
	{
		if (Op->StrikeLine) Op->StrikeLine->SetVisibility(ESlateVisibility::Collapsed);
	}
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
}

void UEclipseInventoryWidget::HandleChipDroppedOutside(FName ItemId, bool bIsClothing)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	// Dragging a slot's contents off the panel puts it back in the room.
	// The spawn logic lives in the subsystem so this, the strip, the DROP
	// button, pickup auto-swap and load migration all drop identically.
	GS->DropItemToWorld(ItemId);

	if (SelectedItemId == ItemId && bSelectedIsClothing == bIsClothing)
	{
		SelectedItemId = NAME_None;
		bSelectedIsClothing = false;
	}
}


void UEclipseInventoryWidget::HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetTabSlot)
{
	// No-op: there is no chip grid to reorder within any more. Items move by
	// being dropped onto a body slot, a carrier, or outside the panel, and
	// each of those is handled by the slot widget or NativeOnDrop.
}

void UEclipseInventoryWidget::HandleStateChanged() { Rebuild(); }

void UEclipseInventoryWidget::EquipChipToSlot(FName ClothingId, EEclipseSlotType Slot)
{
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		if (GS->EquipClothingToSlot(ClothingId))
		{
			UE_LOG(LogEclipse, Log, TEXT("Inv: equipped '%s' → slot %d"),
				*ClothingId.ToString(), (int32)Slot);
		}
	}
	Rebuild();
}

void UEclipseInventoryWidget::UnequipFromSlot(EEclipseSlotType Slot)
{
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		if (GS->UnequipSlot(Slot))
		{
			UE_LOG(LogEclipse, Log, TEXT("Inv: unequipped slot %d"), (int32)Slot);
		}
	}
	Rebuild();
}

void UEclipseInventoryWidget::Rebuild()
{
	if (!WidgetTree) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	{
		FString Hands;   for (const FName& Id : GS->GetItemsInSlot(EEclipseSlotType::Hands))   Hands   += FString::Printf(TEXT("%s,"), *Id.ToString());
		FString Pockets; for (const FName& Id : GS->GetItemsInSlot(EEclipseSlotType::Pockets)) Pockets += FString::Printf(TEXT("%s,"), *Id.ToString());
		FString Worn;    for (const FName& Id : GS->EquippedClothing)                          Worn    += FString::Printf(TEXT("%s,"), *Id.ToString());
		UE_LOG(LogEclipse, Log, TEXT("Inv: Rebuild - hands=[%s] pockets=[%s] worn=[%s]"),
			*Hands, *Pockets, *Worn);
	}

	// The whole screen is the paper doll now, so a rebuild is just repainting
	// every slot from the game state. No chip grid, no wearable pool, and no
	// tab to decide between them.
	UEclipseClothingSlotWidget* AllSlots[] = {
		HeadSlot, EyesSlot, NeckSlot, TopSlot, BottomSlot, ShoesSlot,
		HandsSlot, Pocket0Slot, Pocket1Slot };
	for (UEclipseClothingSlotWidget* S : AllSlots)
	{
		if (S) S->RefreshFromState();
	}

	// Nothing lives outside the doll any more; clear the legacy containers in
	// case an un-repopulated WBP still ships them.
	if (HeldGrid)       HeldGrid->ClearChildren();
	if (EquippedColumn) EquippedColumn->ClearChildren();
	ActiveChips.Reset();

	RefreshChipSelectionStyling();
}

void UEclipseInventoryWidget::SelectItem(FName ItemId, bool bIsClothing)
{
	SelectedItemId = ItemId;
	bSelectedIsClothing = bIsClothing;
	RefreshChipSelectionStyling();
	RefreshDetailPanel();
}

void UEclipseInventoryWidget::RefreshChipSelectionStyling()
{
	for (UEclipseInventoryChipWidget* Chip : ActiveChips)
	{
		if (!Chip) continue;
		const bool bIsSelected = (Chip->GetItemId() == SelectedItemId)
			&& (Chip->IsEquipped() == bSelectedIsClothing);
		Chip->SetSelectedVisual(bIsSelected);
	}
}

void UEclipseInventoryWidget::RefreshDetailPanel()
{
	if (!SelectedNameText || !SelectedDescText) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	if (SelectedItemId.IsNone())
	{
		SelectedNameText->SetText(FText::FromString(TEXT("(select an item)")));
		SelectedDescText->SetText(FText::FromString(TEXT("Click something on the body to inspect it.")));
		if (UseBtn)  UseBtn->SetIsEnabled(false);
		return;
	}

	FString DisplayName = SelectedItemId.ToString();
	FString Description;
	EEclipseItemType ItemType = EEclipseItemType::Usable;
	bool bHasRow = false;

	if (bSelectedIsClothing)
	{
		// Already-equipped item: assume Equippable type. Pull display data
		// from whichever table has the row.
		FEclipseItemRow Row;
		if (GS->GetItemRow(SelectedItemId, Row))
		{
			bHasRow = true;
			ItemType = Row.Type;
			if (!Row.DisplayName.IsEmpty()) DisplayName = Row.DisplayName.ToString();
			if (!Row.Description.IsEmpty()) Description  = Row.Description.ToString();
		}
		else
		{
			FEclipseClothingRow CRow;
			if (GS->GetClothingRow(SelectedItemId, CRow))
			{
				bHasRow = true;
				ItemType = EEclipseItemType::Equippable;
				if (!CRow.DisplayName.IsEmpty()) DisplayName = CRow.DisplayName.ToString();
				if (!CRow.Description.IsEmpty()) Description  = CRow.Description.ToString();
			}
		}
	}
	else
	{
		FEclipseItemRow Row;
		if (GS->GetItemRow(SelectedItemId, Row))
		{
			bHasRow = true;
			ItemType = Row.Type;
			if (!Row.DisplayName.IsEmpty()) DisplayName = Row.DisplayName.ToString();
			if (!Row.Description.IsEmpty()) Description  = Row.Description.ToString();
		}
	}

	SelectedNameText->SetText(FText::FromString(DisplayName.ToUpper()));
	SelectedDescText->SetText(FText::FromString(Description));

	// Two actions now — EQUIP is gone, because wearing something is done by
	// dragging it onto the body slot, not by a button.
	//   USE   — anything UseItem would actually accept
	//   DROP  — anything at all; puts it back in the room
	//
	// This mirrors UseItem's own test rather than the old "RestoreThirst > 0"
	// shortcut, which greyed USE out for items whose whole effect is a
	// permanent stat boost (the perfume).
	bool bCanUse = false;
	if (bHasRow && ItemType == EEclipseItemType::Usable)
	{
		FEclipseItemRow EffRow;
		if (GS->GetItemRow(SelectedItemId, EffRow))
		{
			bCanUse = EffRow.Effect.HeatDelta != 0
			       || EffRow.Effect.ThirstDelta != 0
			       || EffRow.Effect.RestoreThirst > 0.f
			       || (!EffRow.StatBoost.IsNone() && EffRow.StatBoostLevels != 0);
		}
	}

	if (UseBtn)  UseBtn->SetIsEnabled(bCanUse);
}

void UEclipseInventoryWidget::NativeTick(const FGeometry& G, float DeltaTime)
{
	Super::NativeTick(G, DeltaTime);
	TickPendingUse(DeltaTime);
	TickButtonHovers(DeltaTime);
}

// UButton swaps its Hovered brush on the frame the cursor crosses the edge,
// which pops. The styles are all the same red now and the fade lives in the
// BackgroundColor tint, which multiplies the brush — so alpha 0 is "at rest"
// and the interp does the rest.
void UEclipseInventoryWidget::TickButtonHovers(float DeltaTime)
{
	using namespace EclipseUI;
	UButton* Btns[] = { UseBtn, CloseBtn };
	for (int32 i = 0; i < UE_ARRAY_COUNT(Btns); ++i)
	{
		UButton* B = Btns[i];
		if (!B) continue;
		if (!ButtonHoverAlphas.IsValidIndex(i)) ButtonHoverAlphas.SetNum(i + 1);
		float& A = ButtonHoverAlphas[i];
		const float Target = (B->IsHovered() && B->GetIsEnabled()) ? 1.f : 0.f;
		A = FMath::FInterpConstantTo(A, Target, DeltaTime, 6.f);

		// The hover reads as the LABEL coming up, not a card lighting up
		// behind it. These are flat words on the panel — fading a
		// background in and out made them look like chrome that isn't
		// there at rest.
		B->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 0.f));
		constexpr float RestOpacity = 0.55f;
		if (UWidget* Label = B->GetChildAt(0))
		{
			Label->SetRenderOpacity(FMath::Lerp(RestOpacity, 1.f, A));
		}
	}
}

void UEclipseInventoryWidget::OnUse()
{
	if (SelectedItemId.IsNone() || bSelectedIsClothing) return;
	if (!PendingUseId.IsNone()) return;          // already going off

	if (UEclipseAudioSubsystem* A = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		A->PlayCue(EEclipseUiCue::Use);
	}

	// Light up whichever carrier is holding it, then consume a beat later.
	// An item that vanishes on the same frame you click reads as the click
	// having missed; the flash is the receipt.
	UEclipseClothingSlotWidget* Carriers[] = { HandsSlot, Pocket0Slot, Pocket1Slot };
	for (UEclipseClothingSlotWidget* Slot : Carriers)
	{
		if (Slot && Slot->GetOccupant() == SelectedItemId) Slot->FlashUse();
	}

	// Counted down in NativeTick rather than a world timer: the inventory is
	// a modal panel and the world may be paused under it, which would leave
	// a timer-based consume never firing.
	PendingUseId    = SelectedItemId;
	PendingUseTimer = 0.55f;   // let the flare read before the item goes
	SelectedItemId  = NAME_None;   // selection's gone with the item
}

void UEclipseInventoryWidget::TickPendingUse(float DeltaTime)
{
	if (PendingUseId.IsNone()) return;
	PendingUseTimer -= DeltaTime;
	if (PendingUseTimer > 0.f) return;

	const FName Id = PendingUseId;
	PendingUseId = NAME_None;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->UseItem(Id);
	}
}

void UEclipseInventoryWidget::OnCloseClicked()
{
	if (UEclipseAudioSubsystem* A = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr)
	{
		A->PlayCue(EEclipseUiCue::MenuClose);
	}
	Close();
}
