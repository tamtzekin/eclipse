// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseCharacterCreationWidget.generated.h"

class UTextBlock;
class UButton;
class UBorder;
class UWidget;
class UWidgetTree;

// One card on the grid: a dance style or a stat, spent into with the UPGRADE button.
USTRUCT()
struct FEclipseCreationCard
{
	GENERATED_BODY()

	FName Key;            // "hakken" / "aesthetics" — the dance style name or stat key, lowercase
	FString Name;
	FString Blurb;        // shown in the tooltip panel once the card is picked
	bool bDance = false;
	bool bLocked = false; // styles you have to find out in the city
	int32 Level = 0;
};

// Pre-game point spend: a screen of cards, ten points, one UPGRADE button.
UCLASS()
class ECLIPSE_API UEclipseCharacterCreationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Fullscreen, mouse on. StartLevel is opened once they're done.
	static UEclipseCharacterCreationWidget* OpenForPlayer(APlayerController* PC, FName StartLevel);

	// Shared by the C++ fallback and UEclipseUiBuilder::PopulateCharacterCreationWBP.
	static void BuildTree(UWidgetTree* Tree);

	static constexpr int32 StartingPoints = 10;
	static constexpr int32 MaxLevel = 5;

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> PointsValue;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TooltipTitle;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TooltipBody;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> UpgradeBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> UpgradeBtn_Label;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> BeginBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> BeginBtn_Label;

	UFUNCTION() void OnCard0(); UFUNCTION() void OnCard1(); UFUNCTION() void OnCard2();
	UFUNCTION() void OnCard3(); UFUNCTION() void OnCard4(); UFUNCTION() void OnCard5();
	UFUNCTION() void OnCard6(); UFUNCTION() void OnCard7(); UFUNCTION() void OnCard8();
	UFUNCTION() void OnCard9();
	UFUNCTION() void OnUpgrade();
	UFUNCTION() void OnBegin();

private:
	void Select(int32 Index);
	void Refresh();

	TArray<FEclipseCreationCard> Cards;
	int32 Selected = 0;
	int32 Points = StartingPoints;
	FName StartLevelName;
};
