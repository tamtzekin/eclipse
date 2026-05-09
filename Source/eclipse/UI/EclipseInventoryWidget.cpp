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
#include "Blueprint/DragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Data/EclipseItemDefinition.h"
#include "Data/EclipseClothingDefinition.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Inventory chip widget — one per item in the active tab. Holds the item ID
//  + a back-pointer to its owning inventory so the button click routes to
//  UEclipseInventoryWidget::SelectItem (UFUNCTION dynamic delegates can't
//  bind lambdas, so per-chip selection needs its own per-chip class).
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseInventoryChipWidget::InitChip(UEclipseInventoryWidget* InOwner, FName InItemId,
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

void UEclipseInventoryChipWidget::InitEmptySlot(UEclipseInventoryWidget* InOwner, int32 InSlotIndex)
{
	using namespace EclipseUI;

	Owner       = InOwner;
	ItemId      = NAME_None;
	bIsEquipped = false;
	Tint        = Cream;
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
			FLinearColor(0.945f, 0.929f, 0.851f, 0.45f),
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
	using namespace EclipseUI;
	bIsSelected = bSelected;
	if (!ChipFrame) return;
	// Empty cells have their own dim-outline frame styling — never overwrite
	// it with the item-chip selected/unselected brush.
	if (bIsEmpty) return;
	const float OutlineW = bSelected ? 2.f : 1.f;
	const FLinearColor Outline = bSelected ? Cyan : FLinearColor(Tint.R, Tint.G, Tint.B, 0.6f);
	const FLinearColor Fill    = bSelected
		? FLinearColor(Cyan.R, Cyan.G, Cyan.B, 0.10f)
		: FLinearColor(0.945f, 0.929f, 0.851f, 0.04f);
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
			FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),
			FLinearColor(0.945f, 0.929f, 0.851f, 0.6f),
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
		ChipIconText->SetFont(MakeBMSPA(28));
		ChipIconText->SetJustification(ETextJustify::Center);
		ChipIconText->SetColorAndOpacity(FSlateColor(Cream));
		Col->AddChildToVerticalBox(ChipIconText);

		ChipNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ChipNameText"));
		ChipNameText->SetFont(MakeBMSPA(10, 2.f));
		ChipNameText->SetJustification(ETextJustify::Center);
		ChipNameText->SetColorAndOpacity(FSlateColor(CreamDim));
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
	if (Owner)
	{
		Owner->SelectItem(ItemId, bIsEquipped);
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
	using namespace EclipseUI;
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (bIsEmpty || !ChipFrame) return;
	// Subtle highlight on hover so chips feel alive — preserves the tinted
	// outline of the unselected state but bumps the fill brightness.
	ChipFrame->SetBrush(RoundedBrush(
		FLinearColor(0.945f, 0.929f, 0.851f, 0.12f),
		FLinearColor(Tint.R, Tint.G, Tint.B, 0.85f),
		1.f, 4.f));
}

void UEclipseInventoryChipWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	if (bIsEmpty) return;
	// Restore the persisted selected/unselected brush — SetSelectedVisual
	// caches bIsSelected, so calling it again re-applies the correct look.
	SetSelectedVisual(bIsSelected);
}

void UEclipseInventoryChipWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (bIsEmpty) return;

	UDragDropOperation* Op = NewObject<UDragDropOperation>(this);
	Op->Payload  = this;
	Op->Pivot    = EDragPivot::CenterCenter;

	// Build a lightweight visual preview that follows the cursor — a fresh
	// chip widget initialised with the same data, no click handlers.
	UEclipseInventoryChipWidget* Preview =
		CreateWidget<UEclipseInventoryChipWidget>(GetOwningPlayer(), GetClass());
	if (Preview)
	{
		const FString IconStr = ChipIconText ? ChipIconText->GetText().ToString() : TEXT("?");
		const FString NameStr = ChipNameText ? ChipNameText->GetText().ToString() : ItemId.ToString();
		Preview->InitChip(Owner, ItemId, bIsEquipped, IconStr, NameStr, Tint);
		Op->DefaultDragVisual = Preview;
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
	if (Owner)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv[chip]: ▶ DRAG CANCELLED outside panel — dropping '%s' from slot=%d eq=%d"),
			*ItemId.ToString(), SlotIndex, bIsEquipped ? 1 : 0);
		Owner->HandleChipDroppedOutside(ItemId, bIsEquipped);
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

	Owner->HandleChipDroppedOnSlot(Source->ItemId, Source->bIsEquipped, SlotIndex);
	return true;
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

	// Centred chalk panel
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("InventoryPanel"));
	Panel->SetBrush(RoundedBrush(
		FLinearColor(0.039f, 0.043f, 0.059f, 0.97f),
		FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),
		1.f, 8.f));
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
	Title->SetFont(MakeBMSPA(48, 8.f));
	Title->SetColorAndOpacity(FSlateColor(Cyan));
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
		T->SetFont(MakeBMSPA(16, 4.f));
		T->SetColorAndOpacity(FSlateColor(Cream));
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
	SelectedNameText->SetFont(MakeBMSPA(20, 4.f));
	SelectedNameText->SetColorAndOpacity(FSlateColor(Cyan));
	if (UVerticalBoxSlot* VS = DetailPanel->AddChildToVerticalBox(SelectedNameText))
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

	SelectedDescText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SelectedDescText"));
	SelectedDescText->SetText(FText::FromString(TEXT("Click a HELD or EQUIPPED chip above to inspect it.")));
	SelectedDescText->SetColorAndOpacity(FSlateColor(CreamDim));
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
		BS.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.05f));
		BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
		BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.22f));
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);

		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),
			FName(*FString::Printf(TEXT("%s_Label"), *WidgetName.ToString())));
		T->SetText(FText::FromString(Label));
		T->SetColorAndOpacity(FSlateColor(Cream));
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
	EquipBtn  = MakeBtn(TEXT("EQUIP"),  TEXT("EquipBtn"));
	DropBtn   = MakeBtn(TEXT("DROP"),   TEXT("DropBtn"));
	CloseBtn  = MakeBtn(TEXT("CLOSE"),  TEXT("CloseBtn"));
}

void UEclipseInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UseBtn)         UseBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnUse);
	if (EquipBtn)       EquipBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnEquip);
	if (DropBtn)        DropBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnDropItem);
	if (CloseBtn)       CloseBtn->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnCloseClicked);

	if (TabConsumables) TabConsumables->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnTabConsumables);
	if (TabWearables)   TabWearables->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnTabWearables);
	if (TabKey)         TabKey->OnClicked.AddDynamic(this, &UEclipseInventoryWidget::OnTabKey);

	UButton* AllBtns[] = { UseBtn, EquipBtn, DropBtn, CloseBtn,
	                       TabConsumables, TabWearables, TabKey };
	for (UButton* B : AllBtns) if (B) B->SetClickMethod(EButtonClickMethod::MouseDown);

	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseInventoryWidget::HandleStateChanged);
	}

	// Make drag essentially "instant on first cursor move". Default Slate
	// threshold is 5px which feels laggy for inventory chips. The setting
	// is global to FSlateApplication; we restore it in NativeDestruct.
	if (FSlateApplication::IsInitialized())
	{
		SavedDragTriggerDistance = FSlateApplication::Get().GetDragTriggerDistance();
		FSlateApplication::Get().SetDragTriggerDistance(0.f);
	}

	RefreshTabStyling();
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
	UE_LOG(LogEclipse, Log, TEXT("Inv[panel]: drop on panel background — no-op (src='%s' slot=%d)"),
		Source ? *Source->GetItemId().ToString() : TEXT("?"),
		Source ? Source->GetSlotIndex() : -1);
	return true;
}

void UEclipseInventoryWidget::HandleChipDroppedOutside(FName ItemId, bool bIsClothing)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	UE_LOG(LogEclipse, Log, TEXT("Inv: ▶ DROP-TO-WORLD '%s' (eq=%d) — removing from %s"),
		*ItemId.ToString(), bIsClothing ? 1 : 0,
		bIsClothing ? TEXT("EquippedClothing") : TEXT("Inventory"));

	if (bIsClothing) GS->UnequipClothing(ItemId);
	else             GS->RemoveItem(ItemId);

	// Clear selection if the dropped item was selected.
	if (SelectedItemId == ItemId && bSelectedIsClothing == bIsClothing)
	{
		SelectedItemId = NAME_None;
		bSelectedIsClothing = false;
	}
	// HandleStateChanged → Rebuild fires automatically via OnStateChanged
	// broadcast from RemoveItem/UnequipClothing.
}

void UEclipseInventoryWidget::HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetTabSlot)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS || TargetTabSlot < 0)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inv: HandleDroppedOnSlot bail (GS=%p tabSlot=%d)"), GS, TargetTabSlot);
		return;
	}

	UEclipseInventoryChipWidget* TargetChip =
		ActiveChips.IsValidIndex(TargetTabSlot) ? ActiveChips[TargetTabSlot].Get() : nullptr;
	const int32 SrcSlot = GS->ItemSlotPositions.Contains(SourceItemId)
		? GS->ItemSlotPositions[SourceItemId] : INDEX_NONE;

	UE_LOG(LogEclipse, Log, TEXT("Inv: drop request — src='%s' fromSlot=%d → toSlot=%d activeTab=%d"),
		*SourceItemId.ToString(), SrcSlot, TargetTabSlot, ActiveTab);

	if (TargetChip && !TargetChip->IsEmptySlot())
	{
		// Drop onto another item — swap their grid slots. The held-vs-equipped
		// distinction doesn't matter here; we're only shuffling visual
		// positions inside the active tab.
		const FName TargetId = TargetChip->GetItemId();
		if (TargetId == SourceItemId)
		{
			UE_LOG(LogEclipse, Log, TEXT("Inv: drop on self ('%s') — no-op"), *SourceItemId.ToString());
			return;
		}
		UE_LOG(LogEclipse, Log, TEXT("Inv: ✓ SWAP slots — '%s' (slot=%d) ↔ '%s' (slot=%d)"),
			*SourceItemId.ToString(), SrcSlot, *TargetId.ToString(), TargetTabSlot);
		GS->SwapItemSlots(SourceItemId, SrcSlot, TargetId, TargetTabSlot);
	}
	else
	{
		// Drop on empty cell — just move the source's slot to the target.
		UE_LOG(LogEclipse, Log, TEXT("Inv: ✓ MOVE '%s' from slot=%d → slot=%d"),
			*SourceItemId.ToString(), SrcSlot, TargetTabSlot);
		GS->SetItemSlot(SourceItemId, TargetTabSlot);
	}
	// Both mutations broadcast OnStateChanged → Rebuild fires automatically.
}

void UEclipseInventoryWidget::HandleStateChanged() { Rebuild(); }

void UEclipseInventoryWidget::SetActiveTab(int32 TabIndex)
{
	ActiveTab = FMath::Clamp(TabIndex, 0, 2);
	SelectedItemId = NAME_None;
	bSelectedIsClothing = false;
	RefreshTabStyling();
	Rebuild();
	RefreshDetailPanel();
}

void UEclipseInventoryWidget::OnTabConsumables() { SetActiveTab(0); }
void UEclipseInventoryWidget::OnTabWearables()   { SetActiveTab(1); }
void UEclipseInventoryWidget::OnTabKey()         { SetActiveTab(2); }

void UEclipseInventoryWidget::RefreshTabStyling()
{
	using namespace EclipseUI;
	UButton* Tabs[3] = { TabConsumables, TabWearables, TabKey };
	for (int32 i = 0; i < 3; ++i)
	{
		UButton* Btn = Tabs[i];
		if (!Btn) continue;
		const bool bActive = (i == ActiveTab);
		// Find the inner label and re-tint it: cyan for active, dim cream
		// for inactive. Walk the button content rather than relying on
		// per-tab UPROPERTY label refs, which the populator names by
		// convention but we don't bind.
		if (UTextBlock* Label = Cast<UTextBlock>(Btn->GetChildAt(0)))
		{
			Label->SetColorAndOpacity(FSlateColor(bActive ? Cyan : CreamDim));
		}
	}
}

void UEclipseInventoryWidget::Rebuild()
{
	using namespace EclipseUI;

	if (!WidgetTree) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	{
		FString InvList; for (const FName& Id : GS->Inventory) InvList += FString::Printf(TEXT("%s,"), *Id.ToString());
		FString EqList;  for (const FName& Id : GS->EquippedClothing) EqList += FString::Printf(TEXT("%s,"), *Id.ToString());
		UE_LOG(LogEclipse, Log, TEXT("Inv: Rebuild — activeTab=%d  Inventory=[%s]  Equipped=[%s]"),
			ActiveTab, *InvList, *EqList);
	}

	// Clear ItemGrid contents (keep the 18 slot frames built by the
	// populator? — actually re-use, by replacing chip overlay per cell).
	if (ItemGrid) ItemGrid->ClearChildren();
	// Legacy paths: clear any old held/equipped containers if a designer
	// is still on the pre-tab WBP layout. Harmless no-op once they're gone.
	if (HeldGrid) HeldGrid->ClearChildren();
	if (EquippedColumn) EquippedColumn->ClearChildren();

	// Helper that builds a clickable chip for an item ID, looking up its
	// row from the appropriate DataTable for icon + display name.
	auto MakeChip = [&](FName Id, bool bIsClothing) -> UButton*
	{
		FString Icon = TEXT("?");
		FString Name = Id.ToString();
		FLinearColor Tint = Cream;

		if (bIsClothing)
		{
			FEclipseClothingRow Row;
			if (GS->GetClothingRow(Id, Row))
			{
				if (!Row.Icon.IsEmpty()) Icon = Row.Icon;
				if (!Row.DisplayName.IsEmpty()) Name = Row.DisplayName.ToString();
				Tint = Row.TintColor;
			}
		}
		else
		{
			FEclipseItemRow Row;
			if (GS->GetItemRow(Id, Row))
			{
				if (!Row.Icon.IsEmpty()) Icon = Row.Icon;
				if (!Row.DisplayName.IsEmpty()) Name = Row.DisplayName.ToString();
				Tint = Row.TintColor;
			}
		}

		UButton* Btn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
		FButtonStyle BS;
		BS.Normal   = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.04f),
			FLinearColor(Tint.R, Tint.G, Tint.B, 0.6f), 1.f, 4.f);
		BS.Hovered  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.16f),
			FLinearColor::White, 1.f, 4.f);
		BS.Pressed  = RoundedBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.24f),
			FLinearColor::White, 1.f, 4.f);
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		Btn->SetStyle(BS);
		Btn->SetClickMethod(EButtonClickMethod::MouseDown);

		// Chip content: icon (large) + name (small)
		UVerticalBox* ChipCol = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Btn->SetContent(ChipCol);

		UTextBlock* IconText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		IconText->SetText(FText::FromString(Icon));
		IconText->SetJustification(ETextJustify::Center);
		IconText->SetColorAndOpacity(FSlateColor(Tint));
		ChipCol->AddChildToVerticalBox(IconText);

		UTextBlock* NameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		NameText->SetText(FText::FromString(Name.ToUpper()));
		NameText->SetFont(MakeBMSPA(10, 2.f));
		NameText->SetColorAndOpacity(FSlateColor(CreamDim));
		NameText->SetJustification(ETextJustify::Center);
		ChipCol->AddChildToVerticalBox(NameText);

		// Per-chip click routing is deferred to milestone 2 (drag-drop).
		// For now the button is visual-only (SetIsEnabled(false) below)
		// and the action row operates on the auto-selected first item.
		Btn->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s | %s"),
			*Name, bIsClothing ? TEXT("EQUIPPED") : TEXT("HELD"))));

		return Btn;
	};

	// Map the active tab to the item type it should show.
	const EEclipseItemType TabType =
		(ActiveTab == 0) ? EEclipseItemType::Usable :
		(ActiveTab == 1) ? EEclipseItemType::Equippable :
		                   EEclipseItemType::Key;

	// Walk Inventory + EquippedClothing and emit chips for items whose
	// row.Type matches the active tab. Equipped items live in their own
	// array so they go in the same tab as if they were held — designer
	// can later add an "EQUIPPED" badge / overlay.
	struct FTabEntry { FName Id; bool bEquipped = false; };
	TArray<FTabEntry> Entries;
	for (const FName& Id : GS->Inventory)
	{
		FEclipseItemRow Row;
		if (GS->GetItemRow(Id, Row) && Row.Type == TabType)
		{
			Entries.Add({Id, false});
		}
	}
	for (const FName& Id : GS->EquippedClothing)
	{
		// Equipped items: assume Equippable type unless we can confirm
		// otherwise via the item table. Surface them in the Wearables tab.
		bool bAdd = (TabType == EEclipseItemType::Equippable);
		FEclipseItemRow Row;
		if (GS->GetItemRow(Id, Row))
		{
			bAdd = (Row.Type == TabType);
		}
		if (bAdd) Entries.Add({Id, true});
	}

	if (ItemGrid)
	{
		ActiveChips.Reset();
		const int32 MaxSlots = 18;

		// Sparse layout: each item maps to a preferred slot via
		// GS->ItemSlotPositions. Items with no entry yet (e.g. just picked
		// up) get the lowest free slot. ActiveChips is indexed by slot, so
		// HandleChipDroppedOnSlot can route by tab-slot directly.
		ActiveChips.AddZeroed(MaxSlots);

		// Pass 1: place items that already have a slot assigned. If two
		// items collide on the same slot (shouldn't normally happen) the
		// later one is bumped via auto-assign in pass 2.
		TArray<int32> EntrySlot; EntrySlot.SetNum(Entries.Num());
		TArray<bool>  Occupied;  Occupied.SetNumZeroed(MaxSlots);
		for (int32 i = 0; i < Entries.Num(); ++i) EntrySlot[i] = INDEX_NONE;

		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const int32* Stored = GS->ItemSlotPositions.Find(Entries[i].Id);
			if (Stored && *Stored >= 0 && *Stored < MaxSlots && !Occupied[*Stored])
			{
				EntrySlot[i] = *Stored;
				Occupied[*Stored] = true;
			}
		}

		// Pass 2: auto-assign first free slot for unplaced items.
		auto FirstFreeSlot = [&]() -> int32
		{
			for (int32 s = 0; s < MaxSlots; ++s) if (!Occupied[s]) return s;
			return INDEX_NONE;
		};
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			if (EntrySlot[i] != INDEX_NONE) continue;
			const int32 Free = FirstFreeSlot();
			if (Free == INDEX_NONE) break;   // grid is full; remaining items just skip
			EntrySlot[i] = Free;
			Occupied[Free] = true;
		}

		// Build chips at their assigned slots.
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const int32 SlotIdx = EntrySlot[i];
			if (SlotIdx == INDEX_NONE) continue;

			FString IconText = TEXT("?");
			FString NameText = Entries[i].Id.ToString();
			FLinearColor TintColor = EclipseUI::Cream;
			FEclipseItemRow Row_;
			if (GS->GetItemRow(Entries[i].Id, Row_))
			{
				if (!Row_.Icon.IsEmpty())        IconText = Row_.Icon;
				if (!Row_.DisplayName.IsEmpty()) NameText = Row_.DisplayName.ToString();
				TintColor = Row_.TintColor;
			}

			UEclipseInventoryChipWidget* Chip =
				CreateWidget<UEclipseInventoryChipWidget>(this, UEclipseInventoryChipWidget::StaticClass());
			if (!Chip) continue;
			Chip->InitChip(this, Entries[i].Id, Entries[i].bEquipped, IconText, NameText, TintColor, SlotIdx);
			ActiveChips[SlotIdx] = Chip;
		}

		// Fill the rest with empty-mode chips so every cell remains a
		// drop target.
		for (int32 s = 0; s < MaxSlots; ++s)
		{
			if (ActiveChips[s]) continue;
			UEclipseInventoryChipWidget* Empty =
				CreateWidget<UEclipseInventoryChipWidget>(this, UEclipseInventoryChipWidget::StaticClass());
			if (!Empty) continue;
			Empty->InitEmptySlot(this, s);
			ActiveChips[s] = Empty;
		}

		// Mount each chip into its grid cell.
		for (int32 s = 0; s < MaxSlots; ++s)
		{
			UEclipseInventoryChipWidget* Chip = ActiveChips[s];
			if (!Chip) continue;
			const int32 Row = s / 6;
			const int32 Col = s % 6;

			USizeBox* SlotSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			SlotSize->SetWidthOverride(96.f);
			SlotSize->SetHeightOverride(72.f);
			SlotSize->AddChild(Chip);
			if (UUniformGridSlot* GS_ = ItemGrid->AddChildToUniformGrid(SlotSize, Row, Col))
			{
				GS_->SetHorizontalAlignment(HAlign_Fill);
				GS_->SetVerticalAlignment(VAlign_Fill);
			}
		}
	}

	// ── Selection model (milestone 1.5) ──
	// Per-chip click-to-select needs a wrapper UObject per chip (UFUNCTION
	// delegates can't bind lambdas). Action row auto-targets the first
	// item in the active tab. Per-chip click + drag-drop arrive in v2.
	if (Entries.Num() > 0)
	{
		// Re-validate current selection is still in the active tab.
		bool bStillValid = false;
		for (const FTabEntry& E : Entries)
		{
			if (E.Id == SelectedItemId && E.bEquipped == bSelectedIsClothing)
			{
				bStillValid = true;
				break;
			}
		}
		if (!bStillValid)
		{
			SelectedItemId = Entries[0].Id;
			bSelectedIsClothing = Entries[0].bEquipped;
		}
	}
	else
	{
		SelectedItemId = NAME_None;
		bSelectedIsClothing = false;
	}
	RefreshChipSelectionStyling();
	RefreshDetailPanel();
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
		SelectedDescText->SetText(FText::FromString(TEXT("Click a HELD or EQUIPPED chip above to inspect it.")));
		if (UseBtn)   UseBtn->SetIsEnabled(false);
		if (EquipBtn) EquipBtn->SetIsEnabled(false);
		if (DropBtn)  DropBtn->SetIsEnabled(false);
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

	// Type-gated action buttons:
	//   USE   — held items whose row says Usable (drinks, pickups, …)
	//   EQUIP — held items whose row says Equippable (clothing)
	//   DROP  — anything (held drops; equipped unequips back into held)
	const bool bIsHeld     = !bSelectedIsClothing;
	const bool bIsUsable   = bHasRow && ItemType == EEclipseItemType::Usable;
	const bool bIsEquip    = bHasRow && ItemType == EEclipseItemType::Equippable;

	// "Has effect" check for empty containers — RestoreThirst<=0 disables the
	// USE button so empty baggies / glasses read as held-only props.
	bool bHasUseEffect = false;
	if (bIsUsable)
	{
		FEclipseItemRow EffRow;
		if (GS->GetItemRow(SelectedItemId, EffRow))
		{
			bHasUseEffect = EffRow.Effect.RestoreThirst > 0.f;
		}
	}

	if (UseBtn)   UseBtn->SetIsEnabled(bIsHeld && bIsUsable && bHasUseEffect);
	if (EquipBtn) EquipBtn->SetIsEnabled(bIsHeld && bIsEquip);
	if (DropBtn)  DropBtn->SetIsEnabled(true);
}

void UEclipseInventoryWidget::OnUse()
{
	if (SelectedItemId.IsNone() || bSelectedIsClothing) return;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->UseItem(SelectedItemId);
		SelectedItemId = NAME_None;   // selection's gone with the item
	}
}

void UEclipseInventoryWidget::OnEquip()
{
	if (SelectedItemId.IsNone() || bSelectedIsClothing) return;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		// Move from Inventory → EquippedClothing. Slot-exclusivity / proper
		// FEclipseClothingRow lookup will come in a future milestone — for
		// now this just lets you wear anything.
		if (GS->RemoveItem(SelectedItemId))
		{
			GS->EquipClothing(SelectedItemId);
		}
		SelectedItemId = NAME_None;
	}
}

void UEclipseInventoryWidget::OnDropItem()
{
	if (SelectedItemId.IsNone()) return;
	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		if (bSelectedIsClothing) GS->UnequipClothing(SelectedItemId);
		else                      GS->RemoveItem(SelectedItemId);
		SelectedItemId = NAME_None;
	}
}

void UEclipseInventoryWidget::OnCloseClicked() { Close(); }
