// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameStateSubsystem.h"
#include "Eclipse.h"

void UEclipseGameStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("GameStateSubsystem::Initialize"));
}

void UEclipseGameStateSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("GameStateSubsystem::Deinitialize"));
	Super::Deinitialize();
}

bool UEclipseGameStateSubsystem::AddItem(FName ItemId)
{
	if (Inventory.Num() >= InventoryMax)
	{
		UE_LOG(LogEclipse, Log, TEXT("Inventory full!"));
		return false;
	}
	Inventory.Add(ItemId);
	if (ItemId == TEXT("wristband")) bHasWristband = true;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::RemoveItem(FName ItemId)
{
	const int32 Idx = Inventory.IndexOfByKey(ItemId);
	if (Idx == INDEX_NONE) return false;
	Inventory.RemoveAt(Idx);
	if (ItemId == TEXT("wristband")) bHasWristband = false;
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::EquipClothing(FName ClothingId)
{
	if (EquippedClothing.Contains(ClothingId)) return false;
	EquippedClothing.Add(ClothingId);
	NotifyChanged();
	return true;
}

bool UEclipseGameStateSubsystem::UnequipClothing(FName ClothingId)
{
	const int32 N = EquippedClothing.Remove(ClothingId);
	if (N > 0) NotifyChanged();
	return N > 0;
}

void UEclipseGameStateSubsystem::DrainThirst(float Amount)
{
	Thirst = FMath::Clamp(Thirst - Amount, 0.f, MaxThirst);
	NotifyChanged();
}

void UEclipseGameStateSubsystem::GainHeat(float Amount)
{
	Heat = FMath::Clamp(Heat + Amount, 0.f, MaxHeat);
	NotifyChanged();
}

void UEclipseGameStateSubsystem::OnChapterTransition()
{
	++Chapter;
	FailedChoicesThisChapter.Reset();
	UE_LOG(LogEclipse, Log, TEXT("Chapter transition → %d"), Chapter);
	NotifyChanged();
}

bool UEclipseGameStateSubsystem::HasMetNPC(FName Name) const
{
	return MetNPCs.ContainsByPredicate([&](const FEclipseMetNpc& M){ return M.Name == Name; });
}

void UEclipseGameStateSubsystem::RecordMetNPC(FName Name, FName DialogueId)
{
	if (HasMetNPC(Name)) return;
	FEclipseMetNpc M; M.Name = Name; M.DialogueId = DialogueId;
	MetNPCs.Add(M);
	NotifyChanged();
}
