// Copyright (c) ECLIPSE. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EclipseEndScreenWidget.generated.h"

class UButton;
class UTextBlock;
class UEclipseDemoFlow;

/**
 * One-shot listener that turns "the ending dialogue closed" into "show the
 * end screen". Exists because OnDialogueClosed is a DYNAMIC delegate, whose
 * target must be a UObject with a UFUNCTION — a lambda or a plain struct
 * can't subscribe. Declared here rather than in the .cpp because UHT only
 * processes UCLASS declarations found in headers.
 */
UCLASS()
class UEclipseEndArmer : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TWeakObjectPtr<class APlayerController> PC;
	UPROPERTY() TObjectPtr<UEclipseDemoFlow> Flow;

	UFUNCTION()
	void HandleDialogueClosed();
};

/**
 * Demo end screen — REPLAY / QUIT over the ending's black screen.
 *
 * Raised when the ending Ink knot reaches `-> END`. Rather than polling for
 * that, ArmFor() subscribes to UEclipseDialogueSubsystem::OnDialogueClosed:
 * `-> END` closes the dialogue, so that delegate IS the signal. One-shot —
 * it unsubscribes as soon as it fires, so an ordinary conversation ending
 * later can't raise a second end screen.
 *
 * Built via fallback tree; /Game/Justin/UI/WBP_EndScreen is used when present.
 */
UCLASS()
class ECLIPSE_API UEclipseEndScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Subscribe to the dialogue-closed signal so the ending knot's `-> END`
	 *  raises this screen. Call when the ending starts, not when it finishes. */
	static void ArmFor(class APlayerController* PC, UEclipseDemoFlow* Flow);

	UFUNCTION(BlueprintCallable, Category = "Eclipse|UI")
	static UEclipseEndScreenWidget* OpenForPlayer(class APlayerController* PC, UEclipseDemoFlow* Flow);

protected:
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> Title;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    ReplayBtn;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton>    QuitBtn;

private:
	UFUNCTION() void OnReplayClicked();
	UFUNCTION() void OnQuitClicked();

	void BuildFallbackTree();

	UPROPERTY() TObjectPtr<UEclipseDemoFlow> DemoFlow;

	bool bDismissed = false;
};
