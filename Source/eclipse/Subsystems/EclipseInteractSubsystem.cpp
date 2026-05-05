// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseInteractSubsystem.h"
#include "Eclipse.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Items/EclipseItemActor.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Subsystems/EclipseGameStateSubsystem.h"

void UEclipseInteractSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogEclipse, Log, TEXT("InteractSubsystem::Initialize"));
}

void UEclipseInteractSubsystem::Deinitialize()
{
	UE_LOG(LogEclipse, Log, TEXT("InteractSubsystem::Deinitialize"));
	Super::Deinitialize();
}

void UEclipseInteractSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World) return;
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) return;
	APawn* Pawn = PC->GetPawn();
	if (!Pawn) return;

	const FVector PlayerPos = Pawn->GetActorLocation();

	// ── Nearest talkable ──
	AEclipseNpcCharacter* Best = nullptr;
	float BestDistSq = FLT_MAX;
	for (TActorIterator<AEclipseNpcCharacter> It(World); It; ++It)
	{
		AEclipseNpcCharacter* Npc = *It;
		if (!Npc->bTalkable) continue;
		if (Npc->bIsHidden) continue;
		const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), PlayerPos);
		const float Radius = Npc->TalkRadius;
		if (DistSq < Radius * Radius && DistSq < BestDistSq)
		{
			Best = Npc;
			BestDistSq = DistSq;
		}
	}
	if (Best != NearTalkable)
	{
		NearTalkable = Best;
		OnNearTalkableChanged.Broadcast(NearTalkable);
	}

	// ── Nearest item ──
	AEclipseItemActor* BestItem = nullptr;
	float BestItemDistSq = FLT_MAX;
	for (TActorIterator<AEclipseItemActor> It(World); It; ++It)
	{
		AEclipseItemActor* Item = *It;
		if (Item->bPickedUp || Item->IsHidden()) continue;
		const float DistSq = FVector::DistSquared(Item->GetActorLocation(), PlayerPos);
		const float R = Item->PickupRadius;
		if (DistSq < R * R && DistSq < BestItemDistSq)
		{
			BestItem = Item;
			BestItemDistSq = DistSq;
		}
	}
	if (BestItem != NearItem)
	{
		NearItem = BestItem;
		OnNearItemChanged.Broadcast(NearItem);
	}
}

bool UEclipseInteractSubsystem::TryInteract()
{
	if (NearTalkable)
	{
		// Honor thirst block (key NPCs bypass)
		UEclipseGameStateSubsystem* State = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>();
		const bool bBlockedByThirst = State && State->Thirst <= 0.f && !NearTalkable->bIsKeyNPC;
		if (bBlockedByThirst)
		{
			UE_LOG(LogEclipse, Log, TEXT("Thirst=0 — blocked from talking to non-key NPC"));
			return false;
		}
		if (UEclipseDialogueSubsystem* Dlg = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
		{
			return Dlg->OpenDialogue(NearTalkable);
		}
	}
	if (AEclipseItemActor* Item = Cast<AEclipseItemActor>(NearItem))
	{
		Item->Pickup();
		return true;
	}
	return false;
}

void UEclipseInteractSubsystem::SetHighlightActive(bool bActive)
{
	if (bHighlightActive == bActive) return;
	bHighlightActive = bActive;
	OnHighlightToggled.Broadcast(bActive);
}
