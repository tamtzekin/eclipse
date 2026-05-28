// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUD.h"
#include "Eclipse.h"
#include "EclipseInteractWidget.h"
#include "EclipseDialogueWidget.h"
#include "EclipseHUDWidget.h"
#include "EclipseVnPortraitsWidget.h"
#include "EclipseChapterCardWidget.h"
#include "EclipseInventoryStripWidget.h"

void AEclipseHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UEclipseHUDWidget>(PC, HUDWidgetClass);
		// ZOrder 200 keeps the life-meters visible (un-dimmed) over modal
		// overlays. Inventory / Phone / Stats / Pause sit at ZOrder 100
		// (their fullscreen Dim is at the same Z), so a HUD at 200 paints
		// on top of every overlay's dim — the meters stay readable when
		// the player opens an item menu mid-decision.
		// (BlinkWipe at ZOrder 5000 still covers the HUD during scene
		// transitions, which is the desired behaviour.)
		if (HUDWidget) HUDWidget->AddToViewport(200);
	}

	// Permanent left-edge inventory strip — disabled for now; sticking with
	// the I-key modal inventory overlay. Strip widget class
	// (UEclipseInventoryStripWidget) + the IEclipseChipOwner interface stay
	// in the codebase so we can re-enable later by uncommenting this line.
	// UEclipseInventoryStripWidget::CreateForPlayer(PC);

	if (InteractWidgetClass)
	{
		InteractWidget = CreateWidget<UEclipseInteractWidget>(PC, InteractWidgetClass);
		if (InteractWidget) InteractWidget->AddToViewport(5);
	}

	// VN portraits sit BELOW the dialogue panel (z 8) but above the world (z 0)
	if (!VnPortraitsWidgetClass)
		VnPortraitsWidgetClass = UEclipseVnPortraitsWidget::StaticClass();
	VnPortraitsWidget = CreateWidget<UEclipseVnPortraitsWidget>(PC, VnPortraitsWidgetClass);
	if (VnPortraitsWidget) VnPortraitsWidget->AddToViewport(8);

	if (DialogueWidgetClass)
	{
		DialogueWidget = CreateWidget<UEclipseDialogueWidget>(PC, DialogueWidgetClass);
		if (DialogueWidget) DialogueWidget->AddToViewport(10);
	}

	// Chapter card sits ABOVE everything so transitions cover dialogue + HUD.
	if (!ChapterCardWidgetClass)
		ChapterCardWidgetClass = UEclipseChapterCardWidget::StaticClass();
	ChapterCardWidget = CreateWidget<UEclipseChapterCardWidget>(PC, ChapterCardWidgetClass);
	if (ChapterCardWidget) ChapterCardWidget->AddToViewport(50);

	UE_LOG(LogEclipse, Log, TEXT("EclipseHUD: widgets created — Interact=%d Dialogue=%d HUD=%d Vn=%d ChapterCard=%d"),
		InteractWidget != nullptr,
		DialogueWidget != nullptr,
		HUDWidget != nullptr,
		VnPortraitsWidget != nullptr,
		ChapterCardWidget != nullptr);
}
