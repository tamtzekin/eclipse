// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "EclipseChipOwner.generated.h"

/**
 * Routing interface implemented by any widget that hosts UEclipseInventoryChipWidget
 * instances. Lets the chip call back into either the (modal) inventory overlay or
 * the (permanent) inventory strip without knowing which one it belongs to.
 *
 * UE UInterface so we can use the engine's `Cast<IEclipseChipOwner>(UObject)` to
 * cross from a UUserWidget* (the chip's GC-tracked Owner) to the routing methods.
 * The methods are plain C++ virtuals — they don't need Blueprint exposure.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UEclipseChipOwner : public UInterface
{
	GENERATED_BODY()
};

class ECLIPSE_API IEclipseChipOwner
{
	GENERATED_BODY()

public:
	// Chip click — set the host's "currently selected" item.
	virtual void SelectItem(FName ItemId, bool bIsClothing) = 0;

	// Drag released outside any drop-target — host should drop the item to the
	// world (and remove it from inventory). Naming kept for compatibility with
	// existing call sites.
	virtual void HandleChipDroppedOutside(FName ItemId, bool bIsClothing) = 0;

	// Drag released onto another chip / empty slot in the same host — host
	// reorders within its underlying array.
	virtual void HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetSlotIndex) = 0;
};
