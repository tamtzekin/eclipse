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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEclipseGameStateChanged);

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

	// Convenience for tests / Blueprint:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|State")
	void NotifyChanged() { OnStateChanged.Broadcast(); }

	// Inventory cap (matches JS)
	static constexpr int32 InventoryMax = 6;
};
