// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseChipOwner.h"
#include "EclipseInventoryStripWidget.generated.h"

class UVerticalBox;
class UBorder;
class UEclipseInventoryChipWidget;

/**
 * Permanent 6-slot inventory strip anchored to the left edge of the screen.
 * Replaces the modal `I`-key inventory overlay with an always-visible HUD
 * surface that the player can drag chips around inside, or out of (drag-
 * to-world spawns a pickup actor at the player's feet).
 *
 * Layout: a single vertical box of 6 fixed-size cells. Each cell is a
 * UEclipseInventoryChipWidget — occupied chips render the item's icon +
 * label, empty cells render a dim outline and act as drop-targets.
 *
 * Visibility: collapsed while a dialogue is open (the dialogue widget owns
 * the screen), restored on dialogue close. Subscribes to the game state
 * subsystem's OnStateChanged so the chip list rebuilds on pickup / use /
 * drop.
 */
UCLASS()
class ECLIPSE_API UEclipseInventoryStripWidget : public UUserWidget, public IEclipseChipOwner
{
	GENERATED_BODY()

public:
	// Spawn the strip on the player's HUD overlay. Idempotent — returns the
	// existing instance if one's already in the viewport.
	static UEclipseInventoryStripWidget* CreateForPlayer(class APlayerController* PC);

	// IEclipseChipOwner — chip routing.
	virtual void SelectItem(FName ItemId, bool bIsClothing) override;
	virtual void HandleChipDroppedOutside(FName ItemId, bool bIsClothing) override;
	virtual void HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetSlotIndex) override;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// Optional designer binds — populated programmatically if absent.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UVerticalBox> SlotColumn;

private:
	UFUNCTION() void HandleStateChanged();

	void Rebuild();
	void BuildFallbackTree();

	// Cap: 6 slots visible. Matches UEclipseGameStateSubsystem::InventoryMax.
	static constexpr int32 NumSlots = 6;

	// Live chip widgets in the strip — repopulated by Rebuild().
	UPROPERTY()
	TArray<TObjectPtr<UEclipseInventoryChipWidget>> ActiveChips;
};
