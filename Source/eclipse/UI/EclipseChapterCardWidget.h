// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseChapterCardWidget.generated.h"

class UBorder;
class UTextBlock;

/**
 * Fullscreen black overlay used for chapter / level transitions.
 *
 * Lifecycle (one-shot per call to Show):
 *   FadeIn  (0..1 alpha over FadeInSeconds)
 *   Hold    (HoldSeconds — title text visible, screen fully black)
 *   FadeOut (1..0 alpha over FadeOutSeconds)
 *
 * Driven by NativeTick, no UMG animation asset required. Listens to
 * UEclipseGameStateSubsystem::OnChapterCardRequested for trigger.
 *
 * Mirrors the JS prototype's "fade to black + chapter title" beat used at
 * intro and every level transition.
 */
UCLASS()
class ECLIPSE_API UEclipseChapterCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	void Show(const FText& Title);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& InGeometry, float DeltaSeconds) override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UBorder>    BlackBg;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> TitleText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> SubtitleText;

	// Timings (seconds)
	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|UI")
	float FadeInSeconds = 0.6f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|UI")
	float HoldSeconds = 2.4f;

	UPROPERTY(EditDefaultsOnly, Category = "Eclipse|UI")
	float FadeOutSeconds = 0.8f;

	// Sting played once on Show(). Optional — null is no-op.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eclipse|Audio")
	TObjectPtr<class USoundBase> ChapterStingSound;

private:
	enum class EPhase : uint8 { Idle, FadeIn, Hold, FadeOut };
	EPhase Phase = EPhase::Idle;
	float  PhaseSeconds = 0.f;

	UFUNCTION()
	void HandleChapterCardRequested(const FText& Title);

	void SetAlpha(float A);
};
