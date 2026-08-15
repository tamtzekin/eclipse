// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseEntranceTrigger.h"
#include "Eclipse.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseDialogueSubsystem.h"

AEclipseEntranceTrigger::AEclipseEntranceTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	BlockingWall = CreateDefaultSubobject<UBoxComponent>(TEXT("BlockingWall"));
	RootComponent = BlockingWall;
	BlockingWall->SetBoxExtent(FVector(10.f, 150.f, 150.f));
	BlockingWall->SetCollisionProfileName(TEXT("BlockAll"));
	BlockingWall->SetGenerateOverlapEvents(false);
}

void AEclipseEntranceTrigger::BeginPlay()
{
	Super::BeginPlay();

	BlockingWall->OnComponentHit.AddDynamic(this, &AEclipseEntranceTrigger::OnClubEntryInvisibleWall);

	if (UEclipseDialogueSubsystem* DS = GetDialogueSubsystem())
	{
		DS->OnDialogueClosed.AddDynamic(this, &AEclipseEntranceTrigger::HandleDialogueClosed);
	}
}

void AEclipseEntranceTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UEclipseDialogueSubsystem* DS = GetDialogueSubsystem())
	{
		DS->OnDialogueClosed.RemoveDynamic(this, &AEclipseEntranceTrigger::HandleDialogueClosed);
	}
	Super::EndPlay(EndPlayReason);
}

UEclipseDialogueSubsystem* AEclipseEntranceTrigger::GetDialogueSubsystem() const
{
	UGameInstance* GI = GetGameInstance();
	return GI ? GI->GetSubsystem<UEclipseDialogueSubsystem>() : nullptr;
}

void AEclipseEntranceTrigger::OnClubEntryInvisibleWall(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	UE_LOG(LogEclipse, Log, TEXT("EntranceTrigger '%s': OnClubEntryInvisibleWall fired, OtherActor='%s'"),
		*GetName(), *GetNameSafe(OtherActor));

	const ACharacter* Character = Cast<ACharacter>(OtherActor);
	if (!Character || !Character->IsPlayerControlled())
	{
		return;
	}
	if (!GateNpc)
	{
		UE_LOG(LogEclipse, Warning, TEXT("EntranceTrigger '%s': hit by player but GateNpc is not assigned — set it in the Details panel."), *GetName());
		return;
	}

	UEclipseDialogueSubsystem* DS = GetDialogueSubsystem();
	if (!DS)
	{
		UE_LOG(LogEclipse, Warning, TEXT("EntranceTrigger '%s': no UEclipseDialogueSubsystem found."), *GetName());
		return;
	}
	if (DS->IsDialogueOpen())
	{
		return;
	}

	if (!DS->OpenDialogue(GateNpc))
	{
		UE_LOG(LogEclipse, Warning, TEXT("EntranceTrigger '%s': OpenDialogue failed for NPC '%s'"),
			*GetName(), *GetNameSafe(GateNpc));
		return;
	}
	bAwaitingOwnDialogueClose = true;
}

void AEclipseEntranceTrigger::HandleDialogueClosed()
{
	if (!bAwaitingOwnDialogueClose) return;
	bAwaitingOwnDialogueClose = false;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn) return;

	// Turn away from the bouncer before stepping back.
	FRotator NewRotation = PlayerPawn->GetActorRotation();
	NewRotation.Yaw += 180.f;
	PlayerPawn->SetActorRotation(NewRotation);

	if (KnockbackDistance <= 0.f) return;

	// Back along the WALL's own facing, not the player's — the conversation
	// likely turned the player to face the NPC by now.
	const FVector NewLocation = PlayerPawn->GetActorLocation() - GetActorForwardVector() * KnockbackDistance;
	PlayerPawn->SetActorLocation(NewLocation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
}
