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

// Fires after Chapter is incremented by the clock. Subsystems hook this for
// NPC shuffle, weather rolls, music swaps, etc. (Currently nothing else
// subscribes — the hook is exposed so v2+ work can opt in without churning
// the chapter advance code.)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEclipseChapterAdvanced, int32, NewChapter);

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

	// Per-item preferred slot index inside the inventory overlay's 6×3 grid
	// (0..17). Drives sparse "place where I dropped it" layout — items don't
	// re-pack to the left when neighbours are removed. Cleared on removal /
	// unequip. Auto-assigned on add when not already set.
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory")
	TMap<FName, int32> ItemSlotPositions;

	// Item / clothing tables — resolved on subsystem init via the soft paths
	// /Game/Justin/Data/DT_Items and /Game/Justin/Data/DT_Clothing if they
	// exist. The inventory UI walks Inventory + EquippedClothing FName arrays
	// and looks up each entry's display row from these tables.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Inventory")
	TObjectPtr<class UDataTable> ItemTable;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Inventory")
	TObjectPtr<class UDataTable> ClothingTable;

	// ── Quest ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") FEclipseQuestState Quest;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") TArray<FEclipseMetNpc> MetNPCs;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") TSet<FName> FailedChoicesThisChapter;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Quest") bool bVipAccessGranted = false;

	// ── Time ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Time") int32 Chapter = 0;

	// Chapter clock — accumulates real seconds since the chapter started.
	// When it crosses GetChapterDurationSeconds() the chapter auto-advances.
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Time")
	float ChapterElapsedSeconds = 0.f;

	// Default clock length per chapter. Designer-tunable; if a
	// UEclipseChapterDefinition is provided for the current chapter via
	// SetChapterTable, that asset's Duration overrides this fallback.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Time")
	float DefaultChapterDurationSeconds = 90.f;

	// True while the clock is ticking. Auto-paused during dialogue (the
	// player character already skips TickMeters when dialogue is open) and
	// while the world is paused (pause menu).
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Time")
	bool bClockRunning = true;

	// Real-seconds → game-seconds multiplier applied per TickChapterClock.
	//   1.0 — real-time (30 real-sec = 0:00:30 game-time)
	//   2.0 — 2× faster (30 real-sec = 0:01:00 game-time)
	//   0.5 — half-speed (60 real-sec = 0:00:30 game-time)
	// Designer-tunable per build. The HUD readout shows H:MM, so a real-time
	// session naturally takes 60 real-min to advance one game-hour.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Time",
		meta = (ClampMin = "0.05", ClampMax = "20.0"))
	float ClockScale = 1.0f;

	// Optional table of per-chapter names + durations. Index 0 = Chapter 0,
	// index 1 = Chapter 1, etc. Beyond the table → default duration + the
	// fallback "Chapter N" title.
	UPROPERTY(BlueprintReadWrite, Category = "Eclipse|Time")
	TArray<TObjectPtr<class UEclipseChapterDefinition>> ChapterTable;

	// ── Mutations (broadcast on change) ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool AddItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool RemoveItem(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool EquipClothing(FName ClothingId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool UnequipClothing(FName ClothingId);

	// Move an item to a new position inside the Inventory array. Used by the
	// inventory UI's drag-to-reorder. NewIdx is clamped to [0, Inventory.Num()-1].
	// Returns true if anything actually moved (false for no-op or unknown id).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool ReorderInventory(FName ItemId, int32 NewIdx);

	// Same idea, but for the EquippedClothing array.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool ReorderEquippedClothing(FName ClothingId, int32 NewIdx);

	// Set an item's preferred inventory-grid slot. Persists across open/close
	// so manual layouts stick. Broadcasts OnStateChanged.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	void SetItemSlot(FName ItemId, int32 SlotIndex);

	// Swap the slot positions of two items. SlotA/SlotB are the slots they
	// currently occupy (used to seed positions for items that don't yet
	// have an entry in ItemSlotPositions).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	void SwapItemSlots(FName ItemA, int32 SlotA, FName ItemB, int32 SlotB);

	// Consume an item — applies its row's effect (currently: drink restores
	// thirst, hair triggers Quest.bHasHair, eye triggers Quest.bHasEye), then
	// removes it from the inventory. No-op if the item ID isn't held.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool UseItem(FName ItemId);

	// Lookup helpers for the inventory UI. Both accept either a base id
	// ("baggie") or a runtime id ("baggie__Item_baggie_2") — runtime ids
	// have an actor-suffix appended at pickup so multiple instances of the
	// same template can coexist as separate inventory chips. The lookup
	// strips the suffix internally and queries the row by the base id.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool GetItemRow(FName ItemId, struct FEclipseItemRow& OutRow) const;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	bool GetClothingRow(FName ClothingId, struct FEclipseClothingRow& OutRow) const;

	// Strip the runtime "__<actor-suffix>" tail off an inventory id and
	// return just the DT row key. Public because the chip widget needs the
	// base id when comparing chips for selection state etc.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Inventory")
	static FName GetBaseItemId(FName MaybeRuntimeId);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void DrainThirst(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void GainHeat(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Quest")
	void OnChapterTransition();

	// Per-frame clock tick. Hooked from the existing TickMeters call site,
	// so it auto-pauses when meters are paused (dialogue open, pause menu).
	void TickChapterClock(float DeltaSeconds);

	// Read the active chapter's duration from the table, or fall back to
	// DefaultChapterDurationSeconds. Always returns >0.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	float GetChapterDurationSeconds() const;

	// Read the active chapter's title (for the chapter card overlay).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	FText GetChapterTitle() const;

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	void SetClockRunning(bool bRunning) { bClockRunning = bRunning; }

	// Skip directly to the next chapter (debug/cheat). Triggers the same
	// transition flow as the auto-advance.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	void SkipChapter();

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

	// Fires once per chapter advance, AFTER Chapter is incremented and the
	// chapter card has been requested. v2+ subsystems (NPC shuffle, music
	// swap, weather) hook here.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|Time")
	FEclipseChapterAdvanced OnChapterAdvanced;

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
