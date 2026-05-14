// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseBlinkWipeWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "GameFramework/PlayerController.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Static entry points
// ─────────────────────────────────────────────────────────────────────────────
//
//  v2: full-screen black UBorder, animated via SetRenderOpacity(0..1).
//  Previous eyelid-pair approach used RenderTransform.Scale.Y which silently
//  failed to clip the borders to a sub-rect — the wipe wasn't visible at all.
//  Opacity-driven fade is rock-solid and reads cleanly as a "blink".
//
//  Total visible duration ~360 ms (180 close + 180 open). Easy to bump later
//  if the designer wants longer.

namespace
{
	UEclipseBlinkWipeWidget* SpawnWipe(APlayerController* PC)
	{
		if (!PC) return nullptr;
		UEclipseBlinkWipeWidget* W = CreateWidget<UEclipseBlinkWipeWidget>(
			PC, UEclipseBlinkWipeWidget::StaticClass(), TEXT("BlinkWipe"));
		if (!W) return nullptr;
		// Top of everything — blink covers menus, HUD, dialogue, the lot.
		W->AddToViewport(/*ZOrder=*/5000);
		// Block clicks during the wipe but don't steal focus.
		W->SetVisibility(ESlateVisibility::HitTestInvisible);
		return W;
	}
}

UEclipseBlinkWipeWidget* UEclipseBlinkWipeWidget::PlayClose(APlayerController* PC, FOnBlinkPhase OnClosed)
{
	UEclipseBlinkWipeWidget* W = SpawnWipe(PC);
	if (!W) return nullptr;
	W->Mode = EBlinkMode::Close;
	W->Elapsed = 0.f;
	W->bMidpointFired = false;
	W->bFinished = false;
	W->MidpointDelegate = OnClosed;
	W->ApplyEyelidPhase(0.f);
	UE_LOG(LogEclipse, Log, TEXT("BlinkWipe: PlayClose (close=%.2fs)"), W->CloseDuration);
	return W;
}

UEclipseBlinkWipeWidget* UEclipseBlinkWipeWidget::PlayOpen(APlayerController* PC)
{
	UEclipseBlinkWipeWidget* W = SpawnWipe(PC);
	if (!W) return nullptr;
	W->Mode = EBlinkMode::Open;
	W->Elapsed = 0.f;
	W->bMidpointFired = true;     // no midpoint on the open-only phase
	W->bFinished = false;
	W->MidpointDelegate.Unbind();
	W->ApplyEyelidPhase(1.f);     // start fully closed (fully opaque)
	UE_LOG(LogEclipse, Log, TEXT("BlinkWipe: PlayOpen (open=%.2fs)"), W->OpenDuration);
	return W;
}

UEclipseBlinkWipeWidget* UEclipseBlinkWipeWidget::PlayFull(APlayerController* PC, FOnBlinkPhase OnHalfway)
{
	UEclipseBlinkWipeWidget* W = SpawnWipe(PC);
	if (!W) return nullptr;
	W->Mode = EBlinkMode::Full;
	W->Elapsed = 0.f;
	W->bMidpointFired = false;
	W->bFinished = false;
	W->MidpointDelegate = OnHalfway;
	W->ApplyEyelidPhase(0.f);
	UE_LOG(LogEclipse, Log, TEXT("BlinkWipe: PlayFull (total=%.2fs)"),
		W->CloseDuration + W->OpenDuration);
	return W;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle / layout
// ─────────────────────────────────────────────────────────────────────────────

bool UEclipseBlinkWipeWidget::Initialize()
{
	// Pure-C++ user-widget — no BP archetype, so WidgetTree starts null.
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transactional);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("TopEyelid"))))
	{
		BuildFallbackTree();
	}
	return Super::Initialize();
}

void UEclipseBlinkWipeWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Single full-screen black border. We keep the historical UPROPERTY name
	// `TopEyelid` so existing FindWidget("TopEyelid") guards continue to
	// resolve; BottomEyelid stays null (no longer needed for the opacity
	// approach).
	TopEyelid = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("TopEyelid"));
	TopEyelid->SetBrush(SolidBrush(FLinearColor::Black));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(TopEyelid))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
		S->SetAlignment(FVector2D(0.f, 0.f));
	}
	TopEyelid->SetRenderOpacity(0.f);
}

void UEclipseBlinkWipeWidget::ApplyEyelidPhase(float Phase01)
{
	// 0 = fully open (transparent), 1 = fully closed (opaque black).
	const float P = FMath::Clamp(Phase01, 0.f, 1.f);
	if (TopEyelid)
	{
		TopEyelid->SetRenderOpacity(P);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Tick — drive the phase, fire midpoint, tear down at end
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseBlinkWipeWidget::NativeTick(const FGeometry& InGeometry, float DeltaSeconds)
{
	Super::NativeTick(InGeometry, DeltaSeconds);
	if (bFinished) return;

	Elapsed += DeltaSeconds;

	switch (Mode)
	{
	case EBlinkMode::Close:
	{
		const float P = FMath::Clamp(Elapsed / CloseDuration, 0.f, 1.f);
		ApplyEyelidPhase(P);
		if (P >= 1.f)
		{
			if (!bMidpointFired)
			{
				bMidpointFired = true;
				MidpointDelegate.ExecuteIfBound();
			}
			// Close-only: hold black for a moment in case the caller's
			// callback is doing a level swap. Auto-tear-down after 0.5s
			// so we never lock the UI forever if no swap happens.
			if (Elapsed > CloseDuration + 0.5f)
			{
				bFinished = true;
				RemoveFromParent();
			}
		}
		break;
	}

	case EBlinkMode::Open:
	{
		const float P = 1.f - FMath::Clamp(Elapsed / OpenDuration, 0.f, 1.f);
		ApplyEyelidPhase(P);
		if (Elapsed >= OpenDuration)
		{
			bFinished = true;
			RemoveFromParent();
		}
		break;
	}

	case EBlinkMode::Full:
	{
		if (Elapsed < CloseDuration)
		{
			ApplyEyelidPhase(Elapsed / CloseDuration);
		}
		else if (Elapsed < CloseDuration + OpenDuration)
		{
			if (!bMidpointFired)
			{
				bMidpointFired = true;
				MidpointDelegate.ExecuteIfBound();
			}
			const float OpenT = (Elapsed - CloseDuration) / OpenDuration;
			ApplyEyelidPhase(1.f - FMath::Clamp(OpenT, 0.f, 1.f));
		}
		else
		{
			bFinished = true;
			RemoveFromParent();
		}
		break;
	}
	}
}
