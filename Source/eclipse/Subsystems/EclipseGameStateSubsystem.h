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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipsePlayerDied);
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
	// Four-stat system (clubby remix of the original Word/Rhythm/Shadow):
	//   Aesthetics    — taste, fit, presentation. (~old "Word")
	//   Rhythm        — flow, timing, beat-sense.
	//   Zen           — composure, silence. (~old "Shadow")
	//   Psychedelics  — perception, openness to weird input.
	//
	// (Stimulation was previously a fifth stat but has been absorbed into
	// the life-meter system — see Meters block below.)
	//
	// Skill checks reference these via lowercase StatKey strings:
	//   "aesthetics" | "rhythm" | "zen" | "psychedelics"
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Aesthetics   = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Rhythm       = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Zen          = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Stats") int32 Psychedelics = 1;

	// Resolve a lowercase stat-key string ("aesthetics" / "rhythm" / …)
	// to the matching int field. Returns 0 for unknown keys. Used by the
	// dialogue skill-check evaluator and any future "boost a stat by name"
	// systems.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Stats")
	int32 GetStatValue(FName StatKey) const;

	// Adds Delta (can be negative) to the named stat (lowercase key:
	// "aesthetics" / "rhythm" / "zen" / "psychedelics"). Clamped to >= 0.
	// Broadcasts OnStateChanged. Unknown keys log a warning and no-op so a
	// typo in a stage-directions string can't silently desync state.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Stats")
	void ApplyStatDelta(FName StatKey, int32 Delta);

	// ── Life meters (Heat / Thirst / Stimulation) ─────────────────────
	//
	// Integer 0..10 "sweet-spot" model: BOTH extremes are bad. 5 is neutral;
	// the critical zones are ≤2 (too low) and ≥8 (too high). Meters do NOT
	// drain over time — they only move when consumables, dialogue effects,
	// or other explicit events push them via ChangeXxx(Delta). The HUD
	// renders all three bars at identical dimensions with dotted lines at
	// the 2 and 8 boundaries so the player can read at a glance how far
	// each meter is from danger.
	//
	// Semantic per meter (low = bad ↔ high = bad):
	//   HEAT          0 freezing  · 5 comfortable · 10 overheated
	//   THIRST        0 sloshing  · 5 hydrated    · 10 parched
	//   STIMULATION   0 sluggish  · 5 alert       · 10 tweaking
	//
	// Articy gameplay gates these via stage directives like
	//   "HEAT > 8"        — choice available only when overheating
	//   "STIMULATION < 3" — choice available only when fatigued
	// and apply changes via the same syntax as stat changes:
	//   "+1 HEAT", "-2 THIRST", "+3 STIMULATION"
	//
	// Death: ONLY Stimulation == 0 fires OnPlayerDeath. Heat/Thirst at
	// either extreme just lock dialogue gates and surface the critical
	// HUD tint; they don't kill the player directly.
	static constexpr int32 MeterMax          = 10;
	static constexpr int32 MeterCriticalLow  = 2;   // value ≤ this → critical
	static constexpr int32 MeterCriticalHigh = 8;   // value ≥ this → critical

	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") int32 Heat        = 3;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") int32 Thirst      = 5;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Meters") int32 Stimulation = 7;

	// Adds Delta (signed) to the named meter (lowercase "heat" / "thirst" /
	// "stimulation"), clamps to [0, MeterMax], broadcasts OnStateChanged.
	// If the meter is Stimulation and the post-clamp value is 0, also fires
	// OnPlayerDeath (single-shot — won't re-fire if you stay at 0).
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void ChangeMeter(FName MeterKey, int32 Delta);

	// Convenience wrappers for the common case where the caller knows which
	// meter at compile time. All three route through ChangeMeter so the
	// death-trigger + clamp + broadcast logic stays in one place.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void ChangeHeat(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void ChangeThirst(int32 Delta);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	void ChangeStimulation(int32 Delta);

	// Read a meter by name (lowercase "heat" / "thirst" / "stimulation").
	// Returns 0 for unknown keys. Used by the Articy comparison-gate
	// evaluator so "HEAT > 8" reads through this single accessor.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Meters")
	int32 GetMeterValue(FName MeterKey) const;

	// ── Inventory ──
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<FName> Inventory;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<FName> EquippedClothing;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") TArray<int32> Tokens;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Inventory") bool bHasWristband = false;

	// ── Currency ──
	// Counters rather than inventory chips — picking up a "coins" actor adds
	// to Coins, doesn't take a grid slot. The HUD reads these directly. Used
	// by Phase-2 sinks (Bar drinks, Locker, Bouncer bribes).
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Currency") int32 Coins = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Eclipse|Currency") int32 Notes = 0;

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
	// player character skips TickChapterClock when dialogue is open) and
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

	// ── Currency mutators ──
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Currency")
	void AddCoins(int32 Amount);

	// Returns false (and changes nothing) if the player can't afford it.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Currency")
	bool RemoveCoins(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Currency")
	void AddNotes(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Currency")
	bool RemoveNotes(int32 Amount);

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

	// (Old float-scale Drain/Gain APIs removed — see ChangeHeat /
	// ChangeThirst / ChangeStimulation / ChangeMeter above for the new
	// signed-int delta API on the 0..10 integer scale.)

	UFUNCTION(BlueprintCallable, Category = "Eclipse|Quest")
	void OnChapterTransition();

	// Per-frame clock tick. Called from AEclipsePlayerCharacter::Tick;
	// auto-pauses when bClockRunning is false (dialogue open, pause menu).
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

	// Single source of truth for the formatted in-game time-of-day. Returns
	// "H:MM" (24-hour, leading-zero on minutes), wrapping every 24h so a
	// long session never reads as "25:00". The phone widget shows this on
	// its face; any other consumer (HUD, save-load summary, etc.) should
	// call this instead of duplicating the FloorToInt arithmetic.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	FText GetChapterClockText() const;

	// "CH N" chapter label — phone face shows this as a small subtitle
	// under the clock so the player still knows which chapter they're in
	// even though the HUD no longer displays it.
	UFUNCTION(BlueprintCallable, Category = "Eclipse|Time")
	FText GetChapterLabelText() const;

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

	// Fires once when Stimulation transitions from > 0 to 0. The HUD
	// listens and opens the death overlay (TRY AGAIN / QUIT). Reset by
	// Load or by ChangeStimulation lifting the value back above 0.
	UPROPERTY(BlueprintAssignable, Category = "Eclipse|State")
	FEclipsePlayerDied OnPlayerDeath;

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
