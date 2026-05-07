// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "EclipseGameStateSubsystem.generated.h"

/**
 * FQuestState — Angel quest progress.
 * Stages: "intro", "partial", "ready", "saved", "done".
 * (Mirrors the JS `player.quest` shape at index.html ~line 3196.)
 */
USTRUCT(BlueprintType)
struct FEclipseQuestState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Stage = TEXT("intro");
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHasHair = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bHasEye  = false;
};

USTRUCT(BlueprintType)
struct FEclipseMetNpc
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName DialogueId;
};

/** Lightweight summary of a save slot for UI display. */
USTRUCT(BlueprintType)
struct FEclipseSaveSlotInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bExists = false;
	UPROPERTY(BlueprintReadOnly) int32 SlotIndex = 0;
	UPROPERTY(BlueprintReadOnly) FString RoomDisplayName;
	UPROPERTY(BlueprintReadOnly) FName CurrentLevelKey;
	UPROPERTY(BlueprintReadOnly) int32 Chapter = 0;
	UPROPERTY(BlueprintReadOnly) FDateTime SavedAt = FDateTime(0);
	UPROPERTY(BlueprintReadOnly) FString DisplayLabel;   // pre-formatted for menu
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipseGameStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseChapterCardRequested, const FText&, Title);

/**
 * Holds player meta-state across levels. Exposed to UMG via delegates so the
 * HUD widgets re-bind once at construction and react to broadcasts.
 *
 * Mirrors the JS `player` object at index.html ~line 3409:
 *   word/rhythm/shadow/heat/maxHeat/thirst/maxThirst, inventory, equipped
 *   clothing, tokens, quest, metNPCs, hasWristband, vipAccessGranted, chapter.
 */
UCLASS()
class ECLIPSE_API UEclipseGameStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ── Stats ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Word   = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Rhythm = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Shadow = 1;

	// ── Meters ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") float Heat      = 60.f;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") float MaxHeat   = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") float Thirst    = 80.f;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") float MaxThirst = 100.f;

	// Passive drain rates (units per second). Tuned so a freshly-spawned
	// player has ~3 minutes before Thirst empties and ~4 minutes for Heat —
	// long enough to talk to NPCs and explore without being punished.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Meters")
	float ThirstDrainPerSec = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Meters")
	float HeatDrainPerSec = 0.4f;

	// Called by AEclipsePlayerCharacter::Tick. Drains meters and broadcasts
	// OnStateChanged at most once per second (throttled so the HUD bars don't
	// re-render every frame).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void TickMeters(float DeltaSeconds);

	// ── Inventory ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<FName> Inventory;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<FName> EquippedClothing;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<int32> Tokens;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") bool bHasWristband = false;

	// ── Quest ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") FEclipseQuestState Quest;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") TArray<FEclipseMetNpc> MetNPCs;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") TSet<FName> FailedChoicesThisChapter;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") bool bVipAccessGranted = false;

	// ── Time ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Time") int32 Chapter = 0;

	// ── Mutations (broadcast on change) ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool AddItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool RemoveItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool EquipClothing(FName ClothingId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool UnequipClothing(FName ClothingId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void DrainThirst(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void GainHeat(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Quest")
	void OnChapterTransition();

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Quest")
	bool HasMetNPC(FName Name) const;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Quest")
	void RecordMetNPC(FName Name, FName DialogueId);

	// Broadcast whenever any state field changes — UMG widgets bind once.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|State")
	FEclipseGameStateChanged OnStateChanged;

	// Broadcast when a chapter card / title overlay should slide in. The
	// EclipseChapterCardWidget listens for this; gameplay code calls
	// ShowChapterCard() to fire it.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|UI")
	FEclipseChapterCardRequested OnChapterCardRequested;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void ShowChapterCard(const FText& Title) { OnChapterCardRequested.Broadcast(Title); }

	// Convenience for tests / Blueprint:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|State")
	void NotifyChanged() { OnStateChanged.Broadcast(); }

	// ── Save / Load ──
	// Single-slot autosave at "ECLIPSE_AUTOSAVE" (user index 0). Persists every
	// serializable field above; called by EclipseGameInstance::Shutdown for
	// quit-autosave and Init for load-on-boot.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	bool SaveCurrent();

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	bool TryLoadCurrent();

	// Manual slot save/load — slot index 0..NumManualSlots-1. The autosave
	// slot is independent (used by GameInstance + dialogue startGame).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	bool SaveToSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	bool LoadFromSlot(int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	struct FEclipseSaveSlotInfo GetSlotInfo(int32 SlotIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	bool DeleteSlot(int32 SlotIndex);

	// World transform restore. SaveCurrent captures the active player pawn's
	// location/rotation + the room key. TryLoadCurrent restores them if the
	// pawn is in the same level; otherwise it stores them in PendingTeleport*
	// for AEclipsePlayerCharacter::BeginPlay to consume after the new level
	// loads. (Cross-level loads need OpenLevel — not done automatically here;
	// the menu's MainMenu/Load buttons can OpenLevel(CurrentLevelKey) first.)
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Save")
	bool bPendingTeleport = false;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Save")
	FVector PendingTeleportLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Save")
	FRotator PendingTeleportRotation = FRotator::ZeroRotator;

	// Apply (and clear) PendingTeleport* onto the given pawn. No-op if no
	// pending teleport is queued. Called from AEclipsePlayerCharacter::BeginPlay.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Save")
	void ConsumePendingTeleport(class APawn* Pawn);

	// Inventory cap (matches JS)
	static constexpr int32 InventoryMax = 6;

private:
	// Throttle accumulator for OnStateChanged broadcasts during meter drain.
	float MetersBroadcastAccum = 0.f;
};
