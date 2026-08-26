// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseInventoryStripWidget.h"
#include "EclipseInventoryWidget.h"          // UEclipseInventoryChipWidget lives here
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Data/EclipseItemDefinition.h"
#include "Items/EclipseItemActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Spawn factory
// ─────────────────────────────────────────────────────────────────────────────

UEclipseInventoryStripWidget* UEclipseInventoryStripWidget::CreateForPlayer(APlayerController* PC)
{
	if (!PC) return nullptr;

	// Idempotent: if a strip is already alive in this player's viewport,
	// hand back the same instance instead of stacking a second one.
	for (TObjectIterator<UEclipseInventoryStripWidget> It; It; ++It)
	{
		UEclipseInventoryStripWidget* Existing = *It;
		if (Existing && Existing->GetOwningPlayer() == PC && Existing->IsInViewport())
		{
			return Existing;
		}
	}

	UEclipseInventoryStripWidget* W = CreateWidget<UEclipseInventoryStripWidget>(
		PC, UEclipseInventoryStripWidget::StaticClass(), TEXT("InventoryStrip"));
	if (!W) return nullptr;
	W->AddToViewport(/*ZOrder=*/3);   // above HUD (0), below dialogue (10)
	UE_LOG(LogEclipse, Log, TEXT("InventoryStrip: created for %s"), *PC->GetName());
	return W;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseInventoryStripWidget::Initialize()
{
	const bool bSuper = Super::Initialize();

	if (WidgetTree && !WidgetTree->FindWidget(FName(TEXT("SlotColumn"))))
	{
		BuildFallbackTree();
	}

	return bSuper;
}

void UEclipseInventoryStripWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseInventoryStripWidget::HandleStateChanged);
	}

	Rebuild();
}

void UEclipseInventoryStripWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnStateChanged.RemoveDynamic(this, &UEclipseInventoryStripWidget::HandleStateChanged);
		}
	}
	Super::NativeDestruct();
}

void UEclipseInventoryStripWidget::HandleStateChanged() { Rebuild(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Layout — fallback tree (when no WBP is bound)
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseInventoryStripWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Outer panel — left edge of screen, vertically centred.
	UBorder* Frame = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("StripFrame"));
	Frame->SetBrush(RoundedBrush(
		FLinearColor(0.039f, 0.043f, 0.059f, 0.55f),    // dim navy panel
		FLinearColor(0.945f, 0.929f, 0.851f, 0.55f),    // chalk outline
		1.f, 6.f));
	Frame->SetPadding(FMargin(6.f, 8.f));

	if (UCanvasPanelSlot* CSlot = Root->AddChildToCanvas(Frame))
	{
		CSlot->SetAnchors(FAnchors(0.f, 0.5f, 0.f, 0.5f));   // left edge, mid-height
		CSlot->SetAlignment(FVector2D(0.f, 0.5f));
		CSlot->SetAutoSize(true);
		CSlot->SetPosition(FVector2D(20.f, 0.f));
	}

	SlotColumn = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("SlotColumn"));
	Frame->SetContent(SlotColumn);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rebuild — walk Inventory + EquippedClothing, place chips into 6 slots
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseInventoryStripWidget::Rebuild()
{
	using namespace EclipseUI;

	if (!SlotColumn) return;
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	SlotColumn->ClearChildren();
	ActiveChips.Reset();

	// The strip mirrors the carriers on the paper doll: hands first, then
	// pockets, then any worn clothing. There's no free-form arrangement any
	// more: an item's position IS which carrier it's in, so this just lists
	// them in that order and pads the rest with empty drop targets.
	struct FStripEntry { FName Id; bool bEquipped = false; };
	TArray<FStripEntry> SlotMap;
	for (const FName& Id : GS->GetItemsInSlot(EEclipseSlotType::Hands))   SlotMap.Add({Id, false});
	for (const FName& Id : GS->GetItemsInSlot(EEclipseSlotType::Pockets)) SlotMap.Add({Id, false});
	for (const FName& Id : GS->EquippedClothing)                          SlotMap.Add({Id, true});
	while (SlotMap.Num() < NumSlots) SlotMap.Add({NAME_None, false});

	// Build the cells.
	for (int32 i = 0; i < SlotMap.Num(); ++i)
	{
		USizeBox* Cell = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Cell->SetWidthOverride(80.f);
		Cell->SetHeightOverride(80.f);

		UEclipseInventoryChipWidget* Chip = CreateWidget<UEclipseInventoryChipWidget>(
			this, UEclipseInventoryChipWidget::StaticClass());
		if (!Chip) continue;

		const FStripEntry& E = SlotMap[i];
		if (E.Id.IsNone())
		{
			Chip->InitEmptySlot(this, i);
		}
		else
		{
			FString Icon = TEXT("?"), Name = E.Id.ToString();
			FLinearColor Tint = Cream;
			FEclipseItemRow Row;
			if (GS->GetItemRow(E.Id, Row))
			{
				if (!Row.Icon.IsEmpty())        Icon = Row.Icon;
				if (!Row.DisplayName.IsEmpty()) Name = Row.DisplayName.ToString();
				Tint = Row.TintColor;
			}

			// Stacked items show how many. Cigarettes are the only stack in
			// the game — one chip, a count that moves — so the quantity is
			// read from the counter rather than from a per-chip field that
			// every other item would leave at 1.
			if (UEclipseGameStateSubsystem::GetBaseItemId(E.Id) == UEclipseGameStateSubsystem::CigaretteItemId)
			{
				Icon = FString::Printf(TEXT("%d"), GS->Cigarettes);
				Name = FString::Printf(TEXT("%s  x%d"), *Name, GS->Cigarettes);
			}
			Chip->InitChip(this, E.Id, E.bEquipped, Icon, Name, Tint, i);
		}

		Cell->AddChild(Chip);
		ActiveChips.Add(Chip);

		if (UVerticalBoxSlot* VS = SlotColumn->AddChildToVerticalBox(Cell))
		{
			VS->SetPadding(FMargin(0.f, 3.f));
			VS->SetHorizontalAlignment(HAlign_Center);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  IEclipseChipOwner
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseInventoryStripWidget::SelectItem(FName ItemId, bool bIsClothing)
{
	// Strip has no detail panel — for now selection is a logged no-op. Future:
	// could route to a hover-tooltip / inspect overlay.
	UE_LOG(LogEclipse, Log, TEXT("Strip: select '%s' (eq=%d) — no-op for strip"),
		*ItemId.ToString(), bIsClothing ? 1 : 0);
}

void UEclipseInventoryStripWidget::HandleChipDroppedOnSlot(FName SourceItemId, bool bSourceIsClothing, int32 TargetSlotIndex)
{
	// No-op: the strip no longer stores per-item positions, so there is
	// nothing to reorder. An item's place is decided by which carrier holds
	// it, and that's changed on the paper doll, not here.
}

void UEclipseInventoryStripWidget::HandleChipDroppedOutside(FName ItemId, bool bIsClothing)
{
	if (UEclipseGameStateSubsystem* GS = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->DropItemToWorld(ItemId);
	}
}
