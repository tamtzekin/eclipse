// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseGameStateSubsystem.h"
#include "Eclipse.h"
#include "Save/EclipseSaveGame.h"
#include "Kismet/GameplayStatics.h"

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

// ─────────────────────────────────────────────────────────────────────────────
//  Save / Load — single-slot autosave at "ECLIPSE_AUTOSAVE", user index 0.
//
//  Triggered by UEclipseGameInstance::Init (load) and ::Shutdown (save). The
//  save object mirrors the serializable fields on this subsystem.
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseGameStateSubsystem::SaveCurrent()
{
	UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UEclipseSaveGame::StaticClass()));
	if (!Save) return false;

	Save->Word              = Word;
	Save->Rhythm            = Rhythm;
	Save->Shadow            = Shadow;
	Save->Heat              = Heat;
	Save->Thirst            = Thirst;
	Save->Inventory         = Inventory;
	Save->EquippedClothing  = EquippedClothing;
	Save->Tokens            = Tokens;
	Save->bHasWristband     = bHasWristband;
	Save->Quest             = Quest;
	Save->MetNPCs           = MetNPCs;
	Save->bVipAccessGranted = bVipAccessGranted;
	Save->Chapter           = Chapter;

	const bool bOk = UGameplayStatics::SaveGameToSlot(Save,
		UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex);

	UE_LOG(LogEclipse, Log,
		TEXT("SaveCurrent → %s (Heat=%.0f Thirst=%.0f Chapter=%d Inv=%d)"),
		bOk ? TEXT("OK") : TEXT("FAILED"), Heat, Thirst, Chapter, Inventory.Num());
	return bOk;
}

bool UEclipseGameStateSubsystem::TryLoadCurrent()
{
	if (!UGameplayStatics::DoesSaveGameExist(
			UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex))
	{
		UE_LOG(LogEclipse, Log, TEXT("TryLoadCurrent: no save slot, fresh start"));
		return false;
	}

	UEclipseSaveGame* Save = Cast<UEclipseSaveGame>(
		UGameplayStatics::LoadGameFromSlot(
			UEclipseSaveGame::SlotName, UEclipseSaveGame::UserIndex));
	if (!Save)
	{
		UE_LOG(LogEclipse, Warning, TEXT("TryLoadCurrent: slot exists but failed to load"));
		return false;
	}

	Word              = Save->Word;
	Rhythm            = Save->Rhythm;
	Shadow            = Save->Shadow;
	Heat              = Save->Heat;
	Thirst            = Save->Thirst;
	Inventory         = Save->Inventory;
	EquippedClothing  = Save->EquippedClothing;
	Tokens            = Save->Tokens;
	bHasWristband     = Save->bHasWristband;
	Quest             = Save->Quest;
	MetNPCs           = Save->MetNPCs;
	bVipAccessGranted = Save->bVipAccessGranted;
	Chapter           = Save->Chapter;

	NotifyChanged();
	UE_LOG(LogEclipse, Log,
		TEXT("TryLoadCurrent: restored (Heat=%.0f Thirst=%.0f Chapter=%d Inv=%d)"),
		Heat, Thirst, Chapter, Inventory.Num());
	return true;
}
