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

	// Sparse layout: each item lives in the slot it most recently occupied
	// (ItemSlotPositions). Items without a recorded slot get the first free
	// one. Empty slots render as drop-target placeholders.
	struct FStripEntry { FName Id; bool bEquipped = false; };
	TArray<FStripEntry> AllItems;
	for (const FName& Id : GS->Inventory)         AllItems.Add({Id, false});
	for (const FName& Id : GS->EquippedClothing)  AllItems.Add({Id, true});

	// Map slot index → item (or NAME_None if empty).
	TArray<FStripEntry> SlotMap;
	SlotMap.Init({NAME_None, false}, NumSlots);

	// First pass: items with a remembered slot in range.
	TArray<FStripEntry> Pending;
	for (const FStripEntry& E : AllItems)
	{
		const int32* Pref = GS->ItemSlotPositions.Find(E.Id);
		if (Pref && *Pref >= 0 && *Pref < NumSlots && SlotMap[*Pref].Id.IsNone())
		{
			SlotMap[*Pref] = E;
		}
		else
		{
			Pending.Add(E);
		}
	}
	// Second pass: anything else fills the leftmost free slot.
	for (const FStripEntry& E : Pending)
	{
		for (int32 i = 0; i < NumSlots; ++i)
		{
			if (SlotMap[i].Id.IsNone())
			{
				SlotMap[i] = E;
				GS->SetItemSlot(E.Id, i);
				break;
			}
		}
	}

	// Build the 6 cells.
	for (int32 i = 0; i < NumSlots; ++i)
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
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS || TargetSlotIndex < 0 || TargetSlotIndex >= NumSlots) return;

	UEclipseInventoryChipWidget* TargetChip =
		ActiveChips.IsValidIndex(TargetSlotIndex) ? ActiveChips[TargetSlotIndex].Get() : nullptr;

	if (!TargetChip) return;

	if (TargetChip->IsEmptySlot())
	{
		// Drop into an empty slot — just record the new preferred position.
		UE_LOG(LogEclipse, Log, TEXT("Strip: '%s' → empty slot %d"),
			*SourceItemId.ToString(), TargetSlotIndex);
		GS->SetItemSlot(SourceItemId, TargetSlotIndex);
		return;
	}

	// Drop onto an occupied slot — swap the two items.
	const FName TgtId = TargetChip->GetItemId();
	UE_LOG(LogEclipse, Log, TEXT("Strip: swap '%s' ↔ '%s' (slot %d ↔ source slot)"),
		*SourceItemId.ToString(), *TgtId.ToString(), TargetSlotIndex);

	// Source's current slot — look it up via ActiveChips.
	int32 SourceSlot = INDEX_NONE;
	for (int32 i = 0; i < ActiveChips.Num(); ++i)
	{
		if (ActiveChips[i] && ActiveChips[i]->GetItemId() == SourceItemId)
		{
			SourceSlot = i;
			break;
		}
	}
	GS->SwapItemSlots(SourceItemId, SourceSlot, TgtId, TargetSlotIndex);
}

void UEclipseInventoryStripWidget::HandleChipDroppedOutside(FName ItemId, bool bIsClothing)
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	// Spawn a fresh pickup actor at the player's feet so the player can grab
	// the item back. Mesh + tint match the rest of the consumables (cylinder
	// + dark-blue MIC); designers can swap per-item meshes later.
	if (World && Pawn)
	{
		const FVector PawnLoc  = Pawn->GetActorLocation();
		const FVector Forward  = Pawn->GetActorForwardVector();
		// 80 cm in front of the pawn, slightly raised. OnConstruction will
		// floor-snap once the trace fires.
		const FVector SpawnLoc = PawnLoc + Forward * 80.f + FVector(0.f, 0.f, 30.f);

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AEclipseItemActor* Pickup = World->SpawnActor<AEclipseItemActor>(
			AEclipseItemActor::StaticClass(), SpawnLoc, FRotator::ZeroRotator, Params);

		if (Pickup)
		{
			Pickup->ItemId = ItemId;

			// Cylinder placeholder + dark-blue MIC for the visual. Soft-loads
			// so missing-asset cases just render as engine grey.
			if (UStaticMesh* Cyl = Cast<UStaticMesh>(StaticLoadObject(
				UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"))))
			{
				if (Pickup->Mesh) Pickup->Mesh->SetStaticMesh(Cyl);
			}
			if (UMaterialInterface* Mat = Cast<UMaterialInterface>(StaticLoadObject(
				UMaterialInterface::StaticClass(), nullptr,
				TEXT("/Game/Justin/Materials/MI_ItemDarkBlue.MI_ItemDarkBlue"))))
			{
				if (Pickup->Mesh) Pickup->Mesh->SetMaterial(0, Mat);
			}
			Pickup->SetActorScale3D(FVector(0.30f, 0.30f, 0.50f));
			Pickup->SetActorLabel(FString::Printf(TEXT("Item_%s_dropped"), *ItemId.ToString()));
			UE_LOG(LogEclipse, Log, TEXT("Strip: dropped '%s' at %s"),
				*ItemId.ToString(), *SpawnLoc.ToString());
		}
	}

	// Remove from inventory + free the slot. Broadcasts OnStateChanged →
	// Rebuild fires → strip re-renders without this item.
	if (bIsClothing) GS->UnequipClothing(ItemId);
	else             GS->RemoveItem(ItemId);
}
