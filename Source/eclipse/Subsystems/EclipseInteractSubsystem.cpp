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
#include "Player/EclipsePlayerCharacter.h"

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
	// "Eye" point used for the LOS trace — Pawn location is at feet on
	// ACharacter, so lift to mid-torso so partitions / counters at chest
	// height correctly block the trace. ~80 cm above feet ≈ chest/head.
	const FVector PlayerEye = PlayerPos + FVector(0.f, 0.f, 80.f);

	// ── Nearest talkable ──
	AEclipseNpcCharacter* Best = nullptr;
	float BestDistSq = FLT_MAX;

	// Wider, LOS-ignorant candidate for the continuous camera pre-approach
	// (UpdateApproachProximity) — Best only ever gets set once ALREADY
	// inside TalkRadius, so it can't drive a "ramps in as you approach"
	// effect on its own; this tracks the closest talkable NPC full stop,
	// regardless of radius/LOS, purely for that gradual camera swing.
	AEclipseNpcCharacter* ClosestAny = nullptr;
	float ClosestAnyDistSq = FLT_MAX;

	for (TActorIterator<AEclipseNpcCharacter> It(World); It; ++It)
	{
		AEclipseNpcCharacter* Npc = *It;
		if (!Npc->bTalkable) continue;
		if (Npc->bIsHidden) continue;

		const float DistSq = FVector::DistSquared(Npc->GetActorLocation(), PlayerPos);
		if (DistSq < ClosestAnyDistSq)
		{
			ClosestAny = Npc;
			ClosestAnyDistSq = DistSq;
		}

		const float Radius = Npc->TalkRadius;
		if (DistSq >= Radius * Radius || DistSq >= BestDistSq) continue;

		// ── Line-of-sight gate ──
		// Trace from player eye to NPC chest. If anything blocks Visibility
		// (walls, stall partitions, doors-not-yet-set-to-Ignore-Visibility),
		// the NPC drops out of the talkable set. NPCs flagged
		// bIgnoreLineOfSight (audio-only stall voices) bypass this — they're
		// meant to be heard through walls.
		if (!Npc->bIgnoreLineOfSight)
		{
			const FVector NpcChest = Npc->GetActorLocation() + FVector(0.f, 0.f, 60.f);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(EclipseInteractLOS), false, Pawn);
			Params.AddIgnoredActor(Npc);   // don't self-block on the NPC's own collision
			if (AActor* VisualProxy = Npc->VisualProxyActor.Get())
			{
				Params.AddIgnoredActor(VisualProxy);   // don't block on the NPC's own visual double
			}
			FHitResult Hit;
			const bool bBlocked = World->LineTraceSingleByChannel(
				Hit, PlayerEye, NpcChest, ECC_Visibility, Params);
			if (bBlocked)
			{
				UE_LOG(LogEclipse, Verbose, TEXT("LOS blocked: '%s' by %s"),
					*Npc->NpcName.ToString(),
					Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("(no actor)"));
				continue;
			}
		}

		Best = Npc;
		BestDistSq = DistSq;
	}
	if (Best != NearTalkable)
	{
		NearTalkable = Best;
		OnNearTalkableChanged.Broadcast(NearTalkable);
		NearTalkableTimer = 0.f;
	}

	// ── TalkableLockOn ──
	// After a short "notice you're there" delay spent inside a tighter
	// sub-radius of the full interact/TalkRadius, the current nearest
	// talkable turns to face the player. The sub-radius (not the full
	// E-prompt range) is what's checked on the way OUT too, so walking away
	// releases the lock as soon as you clear it, instead of lingering all
	// the way out to the wider E-prompt radius. StartFacePlayer/Tick
	// re-fetch the player's live position every frame, so once locked on,
	// the NPC keeps tracking as the player circles around within that
	// radius.
	//
	// This whole block backs off once a real dialogue is open —
	// EclipseDialogueSubsystem calls the same StartFaceTarget/StopFaceTarget
	// on the player at open/close, and the two fighting over the same flag
	// (e.g. this block releasing on a stray backward step mid-conversation,
	// with nothing re-syncing it) is what made the camera flip-flop/shake.
	// FacingNpc/NearTalkableTimer just hold their pre-dialogue values while
	// paused and resume cleanly once dialogue closes.
	constexpr float FaceDelaySeconds = 0.25f;
	constexpr float LockOnRadiusMultiplier = 0.8f;

	UEclipseDialogueSubsystem* Dlg = World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr;
	const bool bDialogueOpen = Dlg && Dlg->IsDialogueOpen();

	bool bWithinLockOnRadius = false;
	if (!bDialogueOpen)
	{
	if (NearTalkable)
	{
		const float LockOnRadius = NearTalkable->TalkRadius * LockOnRadiusMultiplier;
		bWithinLockOnRadius = FVector::DistSquared(NearTalkable->GetActorLocation(), PlayerPos) <= FMath::Square(LockOnRadius);

		// Walking away breaks the lock immediately, without waiting to
		// physically cross the radius — pressing away from the NPC IS the
		// "let go" gesture, no reason to make the player walk a whole radius
		// width first.
		const FVector Velocity = Pawn->GetVelocity();
		if (bWithinLockOnRadius && !Velocity.IsNearlyZero())
		{
			const FVector ToNpc = (NearTalkable->GetActorLocation() - PlayerPos).GetSafeNormal();
			if (FVector::DotProduct(Velocity.GetSafeNormal(), ToNpc) < 0.f)
			{
				bWithinLockOnRadius = false;
			}
		}
	}

	if (bWithinLockOnRadius)
	{
		NearTalkableTimer += DeltaTime;
	}
	else
	{
		NearTalkableTimer = 0.f;
	}

	// Release whoever was locked on the moment they stop being both the
	// nearest talkable AND inside the tighter radius — covers stepping back
	// out, and switching targets while still technically in range.
	if (FacingNpc.IsValid() && (!bWithinLockOnRadius || FacingNpc.Get() != NearTalkable))
	{
		FacingNpc->StopFacePlayer();
		FacingNpc = nullptr;
		if (AEclipsePlayerCharacter* PlayerChar = Cast<AEclipsePlayerCharacter>(Pawn))
		{
			PlayerChar->StopFaceTarget();
		}
	}

	if (NearTalkable && bWithinLockOnRadius && !FacingNpc.IsValid() && NearTalkableTimer >= FaceDelaySeconds)
	{
		NearTalkable->StartFacePlayer(Pawn);
		FacingNpc = NearTalkable;
		if (AEclipsePlayerCharacter* PlayerChar = Cast<AEclipsePlayerCharacter>(Pawn))
		{
			PlayerChar->StartFaceTarget(NearTalkable->GetActorLocation());
		}
	}
	}

	// Continuous pre-lock camera swing — ramps in well before TalkableLockOn's
	// delay/radius gate fires, using whichever talkable NPC is nearest regardless
	// of LOS/radius. Outer radius = 2.5x TalkRadius, chosen to start the swing a
	// few steps out rather than right at the door.
	if (AEclipsePlayerCharacter* PlayerChar = Cast<AEclipsePlayerCharacter>(Pawn))
	{
		if (ClosestAny)
		{
			const float OuterRadius = ClosestAny->TalkRadius * 2.5f;
			const float Dist = FMath::Sqrt(ClosestAnyDistSq);
			const float Alpha = 1.f - FMath::Clamp((Dist - ClosestAny->TalkRadius) / (OuterRadius - ClosestAny->TalkRadius), 0.f, 1.f);
			PlayerChar->UpdateApproachProximity(Alpha, ClosestAny->GetActorLocation());
		}
		else
		{
			PlayerChar->UpdateApproachProximity(0.f, FVector::ZeroVector);
		}
	}

	// ── Gradual approach turn ──
	// Two distinct radii, same shape as the camera swing above but applied to
	// the NPC's own body: a wide outer radius where the NPC starts leaning
	// toward the player, closing to full-facing right at the tight lock-on
	// radius — where TalkableLockOn's StartFacePlayer then takes over for the
	// committed, tracked lock. Reuses ClosestAny (LOS-ignorant nearest) so it
	// starts before the player is even in talk range, same as the camera.
	if (ApproachTurnNpc.IsValid() && ApproachTurnNpc.Get() != ClosestAny)
	{
		ApproachTurnNpc->UpdateApproachTurn(0.f, nullptr);
		ApproachTurnNpc = nullptr;
	}
	if (ClosestAny)
	{
		constexpr float TurnOuterMultiplier = 3.5f;
		const float TurnLockRadius = ClosestAny->TalkRadius * LockOnRadiusMultiplier;
		const float TurnOuterRadius = ClosestAny->TalkRadius * TurnOuterMultiplier;
		const float Dist = FMath::Sqrt(ClosestAnyDistSq);
		const float TurnAlpha = 1.f - FMath::Clamp((Dist - TurnLockRadius) / (TurnOuterRadius - TurnLockRadius), 0.f, 1.f);
		ClosestAny->UpdateApproachTurn(TurnAlpha, Pawn);
		ApproachTurnNpc = ClosestAny;
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
		// Items with an authored Ink knot show a dialogue panel first (see
		// Items.ink — the "Take it" choice is what actually calls Pickup(),
		// via DispatchMenuAction("takeItem")). Items with no DialogueId keep
		// the old instant-pickup behavior.
		if (Item->DialogueId != NAME_None)
		{
			if (UEclipseDialogueSubsystem* Dlg = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
			{
				return Dlg->OpenItemDialogue(Item);
			}
		}
		Item->Pickup();
		return true;
	}
	return false;
}

void UEclipseInteractSubsystem::SetHighlightActive(bool bActive)
{
	if (bHighlightActive == bActive) return;
	bHighlightActive = bActive;
	UE_LOG(LogEclipse, Log, TEXT("[TAB] SetHighlightActive(%s) — broadcasting to %d listeners"),
		bActive ? TEXT("true") : TEXT("false"),
		OnHighlightToggled.GetAllObjects().Num());
	OnHighlightToggled.Broadcast(bActive);
}
