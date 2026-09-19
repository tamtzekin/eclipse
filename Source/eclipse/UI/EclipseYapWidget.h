// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseYapWidget.generated.h"

class UTextBlock;
class UWidget;
class UWidgetTree;
class ACharacter;

// An overhead one-liner: screen-space text pinned above a character's head that fades in, holds, fades out and removes itself.
// A word wrapped in underscores ("show me you can _muzz_") is picked out in Highlight and glows on every beat.
UCLASS()
class ECLIPSE_API UEclipseYapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static UEclipseYapWidget* Show(ACharacter* Speaker, const FString& Line, float HoldSeconds,
		const FLinearColor& Highlight = FLinearColor::White, float BeatSeconds = 0.f);

	// Shared by the C++ fallback and UEclipseUiBuilder::PopulateYapWBP.
	static void BuildTree(UWidgetTree* Tree);

	// Cuts the hold short and fades out now.
	void FadeOut() { Elapsed = FMath::Max(Elapsed, FadeInSeconds + Hold); }

protected:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// Plain lines wrap in YapText; highlighted ones run Before / Word / After across YapRow.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> YapText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UWidget> YapRow;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> YapBefore;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> YapWord;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> YapAfter;

private:
	static constexpr float FadeInSeconds = 0.4f;
	static constexpr float FadeOutSeconds = 0.6f;

	void SetLine(const FString& Line, const FLinearColor& Highlight);

	TWeakObjectPtr<ACharacter> Speaker;
	float Hold = 4.f;
	float Elapsed = 0.f;
	float Beat = 0.f;             // >0 makes the highlighted word pulse on this period
	FLinearColor HighlightColor = FLinearColor::White;
};
