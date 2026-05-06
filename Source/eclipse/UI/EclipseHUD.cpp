// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseHUD.h"
#include "Eclipse.h"
#include "EclipseInteractWidget.h"
#include "EclipseDialogueWidget.h"
#include "EclipseHUDWidget.h"
#include "EclipseVnPortraitsWidget.h"
#include "EclipseChapterCardWidget.h"

void AEclipseHUD::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (HUDWidgetClass)
	{
		HUDWidget = CreateWidget<UEclipseHUDWidget>(PC, HUDWidgetClass);
		if (HUDWidget) HUDWidget->AddToViewport(0);
	}

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
