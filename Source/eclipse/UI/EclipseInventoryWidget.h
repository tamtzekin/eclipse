// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseInventoryWidget.generated.h"

class UVerticalBox;
class UHorizontalBox;
class UTextBlock;
class UButton;
class UBorder;
class UUniformGridPanel;
class UDragDropOperation;
class UEclipseGameStateSubsystem;
class UEclipseInventoryWidget;

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
	void InitChip(UEclipseInventoryWidget* InOwner, FName InItemId, bool bInEquipped,
	              const FString& IconText, const FString& NameText, const FLinearColor& TintColor,
	              int32 InSlotIndex = INDEX_NONE);

	// Initialise as an empty grid cell — no item, no click, no drag-source,
	// but still a drop-target so dragging onto an empty slot reorders into it.
	void InitEmptySlot(UEclipseInventoryWidget* InOwner, int32 InSlotIndex);

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

	UPROPERTY() TObjectPtr<UEclipseInventoryWidget> Owner;
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
 * Disco Elysium-style inventory overlay. Toggled with `I`, freezes input
 * while open, two-column layout:
 *
 *   ┌────────────────────── INVENTORY ──────────────────────┐
 *   │                                                        │
 *   │   HELD                       EQUIPPED                  │
 *   │   ┌──┐ ┌──┐ ┌──┐             ┌──── HEAD ────┐          │
 *   │   │  │ │  │ │  │             ├──── JACKET ──┤          │
 *   │   └──┘ └──┘ └──┘             └──── NECK ────┘          │
 *   │                                                        │
 *   │   ───────────────  selected item  ──────────────────   │
 *   │   <NAME>                                                │
 *   │   <description text>                                    │
 *   │   [ USE ]  [ EQUIP ]  [ DROP ]  [ CLOSE ]              │
 *   └────────────────────────────────────────────────────────┘
 *
 * Items + clothing are looked up from UEclipseGameStateSubsystem's
 * ItemTable / ClothingTable DataTables. Click a chip to select; use the
 * action row at the bottom to operate on it.
 */
UCLASS()
class ECLIPSE_API UEclipseInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseInventoryWidget* OpenForPlayer(class APlayerController* PC);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Close();

	// Public so individual UEclipseInventoryChipWidget instances can route
	// their button click back to the owning inventory.
	void SelectItem(FName ItemId, bool bIsClothing);

	// Called by a chip widget when its drag was cancelled (i.e. dropped
	// outside any drop-target). For the inventory that means dropped
	// outside the chalk panel → drop the item from inventory.
	void HandleChipDroppedOutside(FName ItemId, bool bIsClothing);

	// Called by a chip widget when another chip is dropped onto it (or
	// onto an empty cell). Reorders the source item to the target tab
	// slot inside the active tab's underlying array.
	void HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetTabSlot);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

	// New tabbed layout: 3 tab buttons + a 6×3 ItemGrid + detail row.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           TabConsumables;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           TabWearables;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           TabKey;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UUniformGridPanel> ItemGrid;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>        SelectedNameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock>        SelectedDescText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           UseBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           EquipBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           DropBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>           CloseBtn;

	// Legacy bind names — left for back-compat with older WBPs that
	// haven't been re-populated to the tabbed layout yet.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UHorizontalBox>    HeldGrid;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox>      EquippedColumn;

private:
	UFUNCTION() void OnUse();
	UFUNCTION() void OnEquip();
	UFUNCTION() void OnDropItem();
	UFUNCTION() void OnCloseClicked();

	UFUNCTION() void OnTabConsumables();
	UFUNCTION() void OnTabWearables();
	UFUNCTION() void OnTabKey();

	UFUNCTION() void HandleStateChanged();

	void Rebuild();
	void RefreshDetailPanel();
	void RefreshTabStyling();
	void RefreshChipSelectionStyling();
	void SetActiveTab(int32 TabIndex);

	void BuildFallbackTree();

	// Track which item is currently selected and whether it's a clothing
	// row (drives EQUIP vs USE button visibility / behaviour).
	FName SelectedItemId;
	bool  bSelectedIsClothing = false;

	// Active tab. 0=Consumables (Usable), 1=Wearables (Equippable), 2=Key.
	int32 ActiveTab = 0;

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
