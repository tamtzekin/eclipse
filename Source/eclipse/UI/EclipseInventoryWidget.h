// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/DragDropOperation.h"
#include "EclipseChipOwner.h"
#include "Data/EclipseClothingDefinition.h"   // EEclipseSlotType
#include "EclipseInventoryWidget.generated.h"

class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UButton;
class UBorder;
class UWidget;
class UUniformGridPanel;
class UDragDropOperation;
class UEclipseGameStateSubsystem;
class UEclipseInventoryWidget;

/**
 * Drag operation for inventory chips. Carries the source chip as Payload
 * plus a reference to a "strike-through" line widget baked into the drag
 * visual — the inventory panel toggles that line on while the cursor is
 * over a clothing slot the item can't go into, giving the classic
 * crossed-out "no" feedback.
 */
UCLASS()
class ECLIPSE_API UEclipseInventoryDragOp : public UDragDropOperation
{
	GENERATED_BODY()

public:
	// Strike line inside DefaultDragVisual — Collapsed normally, shown
	// when hovering an incompatible slot.
	UPROPERTY() TObjectPtr<UWidget> StrikeLine;
};

/**
 * One inventory chip — used inside UEclipseInventoryWidget's grid. Each chip
 * holds the item ID + a back-reference to the inventory it lives in, so the
 * button click routes through to UEclipseInventoryWidget::SelectItem. This
 * is the wrapper UObject the milestone-1 design was missing (UFUNCTION
 * dynamic delegates can't bind lambdas, so per-chip selection needs its
 * own per-chip class).
 */
UCLASS()
class ECLIPSE_API UEclipseInventoryChipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Owner is any UUserWidget that also implements IEclipseChipOwner — i.e.
	// either the (modal) UEclipseInventoryWidget or the (permanent strip)
	// UEclipseInventoryStripWidget. The chip stores it as UUserWidget* for
	// GC; chip→owner calls cast to IEclipseChipOwner at the call site.
	void InitChip(UUserWidget* InOwner, FName InItemId, bool bInEquipped,
	              const FString& IconText, const FString& NameText, const FLinearColor& TintColor,
	              int32 InSlotIndex = INDEX_NONE);

	// Initialise as an empty grid cell — no item, no click, no drag-source,
	// but still a drop-target so dragging onto an empty slot reorders into it.
	void InitEmptySlot(UUserWidget* InOwner, int32 InSlotIndex);

	void SetSelectedVisual(bool bSelected);

	FName GetItemId() const { return ItemId; }
	bool  IsEquipped() const { return bIsEquipped; }
	int32 GetSlotIndex() const { return SlotIndex; }
	bool  IsEmptySlot() const { return bIsEmpty; }

protected:
	virtual bool Initialize() override;

	// Drag-drop: pressing left mouse on a chip arms a drag; releasing the
	// drag over another chip / empty cell in the same panel reorders;
	// releasing outside the panel drops the item to the world.
	// MouseButtonUp without a drag = click → SelectItem.
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	// Accept incoming drops from other chips. NativeOnDragOver must return
	// true so Slate considers this widget a valid drop target; NativeOnDrop
	// resolves the swap/insert via the owning inventory widget.
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ChipButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder>    ChipFrame;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChipIconText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> ChipNameText;

private:
	UFUNCTION() void OnChipClicked();

	// Owner widget — either UEclipseInventoryWidget or
	// UEclipseInventoryStripWidget. Stored as UUserWidget* for GC; cast to
	// IEclipseChipOwner at every call site (see EclipseInventoryWidget.cpp).
	UPROPERTY() TObjectPtr<UUserWidget> Owner;
	FName        ItemId;
	bool         bIsEquipped = false;
	FLinearColor Tint = FLinearColor::White;

	// Cached selection state so NativeOnMouseLeave can restore the correct
	// brush after a hover (otherwise we'd clobber a selected chip's cyan
	// outline back to the unselected look).
	bool bIsSelected = false;

	// Position of this chip inside the active tab's 6×3 grid (0..17). -1 for
	// the floating drag-preview clones that aren't part of any grid.
	int32 SlotIndex = INDEX_NONE;

	// True when this widget represents an empty cell — drop target only,
	// no item id, no click handler, no drag source.
	bool bIsEmpty = false;
};

/**
 * One wearable slot — a drop target inside the inventory overlay. Each
 * slot has a fixed EEclipseSlotType (Head / Eyes / Neck / Top / Bottom /
 * Shoes) and accepts drops of clothing chips whose DT_Clothing row
 * matches that slot type. Dropping a matching chip calls the parent
 * inventory widget back to fire EquipClothingToSlot on the GameState.
 * The slot also fires drag operations of its own — dragging the
 * equipped item OUT goes through the regular chip-drop flow which lands
 * the chip back in the inventory grid.
 */
UCLASS()
class ECLIPSE_API UEclipseClothingSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Set by the parent inventory widget when constructed. Determines
	// which clothing rows this slot accepts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eclipse|Slot")
	EEclipseSlotType SlotType = EEclipseSlotType::Head;

	// Which cell within a multi-capacity carrier this widget draws. Pockets
	// hold two, so two widgets share SlotType=Pockets with CellIndex 0 and 1.
	// Always 0 for the one-garment body slots.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Eclipse|Slot")
	int32 CellIndex = 0;

	// Hands / Pockets carry loose inventory items (via ItemPlacements)
	// rather than worn clothing (via EquippedSlots). The two paths differ
	// in what they accept and what dropping does, so every handler branches
	// on this.
	bool IsCarrier() const;

	// The item this specific cell currently shows, or NAME_None if empty.
	FName GetOccupant() const;

	// Back-pointer so drop handlers can call into the inventory widget
	// to do the actual equip/unequip.
	UPROPERTY()
	TObjectPtr<class UEclipseInventoryWidget> OwningInventory;

	// Reads the currently-equipped item for this slot from the GameState
	// and updates the label / icon. Called whenever the inventory widget
	// rebuilds.
	void RefreshFromState();

	// Drag-over feedback state, driven by the inventory panel while a chip
	// is dragged over this slot. 0 = idle, 1 = valid target (green),
	// 2 = invalid target (greyed out).
	enum class EHoverState : uint8 { Idle, Valid, Invalid };
	void SetHoverFeedback(EHoverState State);

protected:
	virtual bool Initialize() override;

	// Clicking an occupied slot selects its item, which is what lights up
	// USE / DROP in the detail panel below. Without this the only way to act
	// on something you're wearing would be to drag it somewhere first.
	virtual FReply NativeOnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;

	virtual bool NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
	virtual void NativeOnDragLeave(const FDragDropEvent&, UDragDropOperation*) override;
	virtual bool NativeOnDrop(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
	virtual void NativeOnDragDetected(const FGeometry&, const FPointerEvent&, UDragDropOperation*& Op) override;

	// Slot label ("HEAD" / "EYES" / etc.) — set from SlotType on construct.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SlotLabel;

	// Icon glyph for the equipped item. Empty when slot is empty.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SlotIcon;

	// Frame border — tint flashes briefly on a valid drop accept.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder>    SlotFrame;
};

/**
 * The inventory. Toggled with `I`, freezes input while open. One screen —
 * the paper doll — with no tabs, no chip grid and no "available" list:
 *
 *   ┌──────────────────── INVENTORY ─────────────────────┐
 *   │   ┌─ HEAD ─┐                       ┌─ EYES ──┐     │
 *   │   ┌─ NECK ─┐        ▟█▙            ┌─ TOP ───┐     │
 *   │   ┌─ HANDS ┐        ███            ┌─ BOTTOM ┐     │
 *   │   ┌─ PKT 0 ┐        █ █            ┌─ SHOES ─┐     │
 *   │   ┌─ PKT 1 ┐                                       │
 *   │   ──────────────  selected item  ───────────────   │
 *   │   <NAME>                                            │
 *   │   <description text>                                │
 *   │   [ USE ]                        [ CLOSE ]         │
 *   └─────────────────────────────────────────────────────┘
 *
 * Everything you own is worn, in your hands, or in a pocket. Anything else
 * is on the floor or in a locker — see UEclipseGameStateSubsystem's
 * ItemPlacements / GetSlotCapacity for the carry rules.
 *
 * Click a slot to select what's in it, then USE. There is no EQUIP button:
 * wearing something means dragging it onto the body slot, and taking it off
 * means dragging it into a hand or pocket. There is no DROP button either —
 * you give something up by swapping it for a pickup you can't carry
 * (UEclipseSwapPromptWidget), never by discarding it into nothing.
 *
 * Styled as hyperlink blue on white in the dialogue font (MakeRodin).
 */
UCLASS()
class ECLIPSE_API UEclipseInventoryWidget : public UUserWidget, public IEclipseChipOwner
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseInventoryWidget* OpenForPlayer(class APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

	// IEclipseChipOwner — chip widgets call back through these.
	virtual void SelectItem(FName ItemId, bool bIsClothing) override;
	virtual void HandleChipDroppedOutside(FName ItemId, bool bIsClothing) override;
	virtual void HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetTabSlot) override;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	// Resolve which clothing slot (if any) the screen point is over.
	// Shared by drag-over (live feedback) and drop (equip resolution).
	UEclipseClothingSlotWidget* SlotUnderPoint(const FVector2D& ScreenPos) const;
	void ResetSlotHovers();

	// Single-screen layout: the paper doll IS the inventory. There are no
	// tabs, no chip grid, and no "available" pool — every item you have is
	// worn, in your hands, or in a pocket, and anything else is on the floor
	// or in a locker.

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>        SelectedNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>        SelectedDescText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           UseBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           CloseBtn;

	// Legacy bind names — left for back-compat with older WBPs that
	// haven't been re-populated to the tabbed layout yet.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox>    HeldGrid;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox>      EquippedColumn;

	// ── Wearable slot drop targets ─────────────────────────────────────
	// Six instances live in a horizontal strip above the chip grid. The
	// inventory widget owns them so it can iterate / refresh them when
	// the GameState changes. They're built in NativeConstruct if not
	// already bound by the WBP populator.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> HeadSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> EyesSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> NeckSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> TopSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> BottomSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> ShoesSlot;

	// ── Carrier slots ──────────────────────────────────────────────────
	// Hands takes one item of any size; the two pocket cells take one
	// Small item each. Both share the slot widget with the clothing slots —
	// see UEclipseClothingSlotWidget::IsCarrier for where behaviour forks.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> HandsSlot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> Pocket0Slot;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UEclipseClothingSlotWidget> Pocket1Slot;

public:
	// Called by UEclipseClothingSlotWidget::NativeOnDrop after a valid
	// chip-on-slot drop. Forwards to GameState's EquipClothingToSlot
	// then rebuilds the panel so the chip leaves the grid and the slot
	// shows it equipped.
	void EquipChipToSlot(FName ClothingId, EEclipseSlotType Slot);

	// Called when the player drags an equipped item OUT of a slot.
	// Forwards to GameState's UnequipSlot then rebuilds.
	void UnequipFromSlot(EEclipseSlotType Slot);

private:
	UFUNCTION() void OnUse();
	UFUNCTION() void OnCloseClicked();

	UFUNCTION() void HandleStateChanged();

	void Rebuild();
	void RefreshDetailPanel();
	void RefreshChipSelectionStyling();

	void BuildFallbackTree();

	// Track which item is currently selected and whether it's a clothing
	// row (drives EQUIP vs USE button visibility / behaviour).
	FName SelectedItemId;
	bool  bSelectedIsClothing = false;

	// Slate's drag-trigger distance is global; we lower it on open so chips
	// snap to the cursor on the slightest motion (basically "click-and-hold
	// to drag"), then restore the original value on close so other parts of
	// the app keep their normal click-vs-drag behaviour.
	float SavedDragTriggerDistance = -1.f;

	// Live chip widgets in the active tab — repopulated by Rebuild().
	// Used by RefreshChipSelectionStyling to highlight the selected one.
	UPROPERTY()
	TArray<TObjectPtr<UEclipseInventoryChipWidget>> ActiveChips;
};
