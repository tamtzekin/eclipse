// Copyright (c) ECLIPSE. All Rights Reserved.

#include "EclipseStatsMenuWidget.h"
#include "Eclipse.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

UEclipseStatsMenuWidget* UEclipseStatsMenuWidget::OpenForPlayer(APlayerController* PC)
{
	// VERSION MARKER — if you DON'T see "[v3]" in the log after pressing C,
	// the running binary is stale and Live Coding hasn't picked up this file.
	UE_LOG(LogEclipse, Warning, TEXT("StatsMenu: OpenForPlayer [v3] entered"));

	if (!PC) { UE_LOG(LogEclipse, Warning, TEXT("StatsMenu: PC is null")); return nullptr; }

	TSubclassOf<UEclipseStatsMenuWidget> Cls = UEclipseStatsMenuWidget::StaticClass();
	if (UClass* BPClass = LoadClass<UEclipseStatsMenuWidget>(nullptr,
		TEXT("/Game/Justin/UI/WBP_StatsMenu.WBP_StatsMenu_C")))
	{
		Cls = BPClass;
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu: using WBP_StatsMenu_C"));
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu: WBP not found, using C++ fallback class"));
	}

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: about to CreateWidget"));
	UEclipseStatsMenuWidget* W = CreateWidget<UEclipseStatsMenuWidget>(PC, Cls, TEXT("StatsMenu"));
	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: CreateWidget returned %s"), W ? TEXT("OK") : TEXT("nullptr"));
	if (!W) return nullptr;

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: AddToViewport"));
	W->AddToViewport(/*ZOrder=*/100);

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: SetIsFocusable"));
	W->SetIsFocusable(true);

	// Tree dump — should ALWAYS produce a line, even if tree is empty/null.
	if (W->WidgetTree)
	{
		TArray<UWidget*> All;
		W->WidgetTree->GetAllWidgets(All);
		UE_LOG(LogEclipse, Warning, TEXT("StatsMenu: tree has %d widgets, root=%s"),
			All.Num(),
			W->WidgetTree->RootWidget ? *W->WidgetTree->RootWidget->GetName() : TEXT("none"));
		for (UWidget* Wd : All)
		{
			if (Wd)
			{
				UE_LOG(LogEclipse, Log, TEXT("  - %s (%s)"),
					*Wd->GetName(), *Wd->GetClass()->GetName());
			}
		}
	}
	else
	{
		UE_LOG(LogEclipse, Error, TEXT("StatsMenu: WidgetTree is NULL after CreateWidget!"));
	}

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: SetGamePaused(true)"));
	UGameplayStatics::SetGamePaused(W->GetWorld(), true);

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: applying FInputModeGameAndUI"));
	FInputModeGameAndUI Mode;
	Mode.SetWidgetToFocus(W->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);
	W->SetKeyboardFocus();

	UE_LOG(LogEclipse, Warning, TEXT("StatsMenu: opened for %s [v3 DONE]"), *PC->GetName());
	return W;
}

void UEclipseStatsMenuWidget::Close()
{
	// Re-entry guard. NativeOnKeyDown and the PC's C-binding can both reach
	// here on the same press; we want the second call to be a no-op.
	if (bClosing) return;
	bClosing = true;

	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: Close()"));

	APlayerController* PC = GetOwningPlayer();
	if (UWorld* W = GetWorld())
	{
		UGameplayStatics::SetGamePaused(W, false);
	}
	if (PC)
	{
		FInputModeGameOnly Mode;
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(false);
	}
	RemoveFromParent();
}

bool UEclipseStatsMenuWidget::Initialize()
{
	UE_LOG(LogEclipse, Log, TEXT("StatsMenu::Initialize — WidgetTree=%s"),
		WidgetTree ? TEXT("set") : TEXT("null"));

	// Pure-C++ UUserWidget classes have no Widget Blueprint archetype, so
	// WidgetTree starts null. Allocate one ourselves so BuildFallbackTree has
	// a place to ConstructWidget into. (BP-class paths leave WidgetTree alone
	// — the BP's archetype already populated it.)
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transactional);
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu::Initialize — allocated empty WidgetTree"));
	}

	if (!WidgetTree->FindWidget(FName(TEXT("AestheticsRow"))))
	{
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu::Initialize — building fallback tree"));
		BuildFallbackTree();
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu::Initialize — fallback built, root=%s"),
			WidgetTree->RootWidget ? *WidgetTree->RootWidget->GetName() : TEXT("STILL NONE"));
	}
	else
	{
		UE_LOG(LogEclipse, Log, TEXT("StatsMenu::Initialize — fallback skipped (AestheticsRow already present)"));
	}
	return Super::Initialize();
}

void UEclipseStatsMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CloseBtn)
	{
		CloseBtn->SetClickMethod(EButtonClickMethod::MouseDown);
		CloseBtn->OnClicked.AddDynamic(this, &UEclipseStatsMenuWidget::OnCloseClicked);
	}

	if (UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr)
	{
		GS->OnStateChanged.AddDynamic(this, &UEclipseStatsMenuWidget::HandleStateChanged);
	}

	RefreshAll();
}

void UEclipseStatsMenuWidget::NativeDestruct()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UEclipseGameStateSubsystem* GS = GI->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->OnStateChanged.RemoveDynamic(this, &UEclipseStatsMenuWidget::HandleStateChanged);
		}
	}
	Super::NativeDestruct();
}

FReply UEclipseStatsMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	UE_LOG(LogEclipse, Log, TEXT("StatsMenu: NativeOnKeyDown key=%s"), *K.ToString());
	// Only handle Escape here. C is owned by the PC's legacy KeyBinding so the
	// open/close path stays in one place (and `bExecuteWhenPaused=true` keeps
	// it firing while the menu pauses the world). Under FInputModeGameAndUI
	// the PC binding wins, so we don't double-close.
	if (K == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UEclipseStatsMenuWidget::OnCloseClicked() { Close(); }
void UEclipseStatsMenuWidget::HandleStateChanged() { RefreshAll(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Layout — fallback tree
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseStatsMenuWidget::BuildFallbackTree()
{
	using namespace EclipseUI;

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("Canvas_0"));
	WidgetTree->RootWidget = Root;

	// Full-screen dim
	UBorder* Dim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.65f)));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Dim))
	{
		S->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		S->SetOffsets(FMargin(0.f));
	}

	// Centred panel
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StatsPanel"));
	Panel->SetBrush(RoundedBrush(
		FLinearColor(0.039f, 0.043f, 0.059f, 0.97f),
		FLinearColor(0.945f, 0.929f, 0.851f, 0.85f),
		1.f, 8.f));
	Panel->SetPadding(FMargin(36.f, 28.f));
	Panel->SetHorizontalAlignment(HAlign_Fill);
	Panel->SetVerticalAlignment(VAlign_Fill);
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Panel))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(560.f, 520.f));
		S->SetZOrder(1);
	}

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StatsColumn"));
	Panel->SetContent(Column);

	// Title
	UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StatsTitle"));
	Title->SetText(FText::FromString(TEXT("STATS")));
	Title->SetFont(MakeBMSPA(48, 8.f));
	Title->SetColorAndOpacity(FSlateColor(Cyan));
	Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(Title))
	{
		VS->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
		VS->SetHorizontalAlignment(HAlign_Center);
	}

	// Helper — one stat row.
	auto MakeStatRow = [&](FName WidgetName) -> UTextBlock*
	{
		UTextBlock* T = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), WidgetName);
		T->SetFont(MakeBMSPA(20, 4.f));
		T->SetColorAndOpacity(FSlateColor(Cream));
		if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(T))
		{
			VS->SetPadding(FMargin(0.f, 4.f));
		}
		return T;
	};

	AestheticsRow   = MakeStatRow(TEXT("AestheticsRow"));
	RhythmRow       = MakeStatRow(TEXT("RhythmRow"));
	ZenRow          = MakeStatRow(TEXT("ZenRow"));
	PsychedelicsRow = MakeStatRow(TEXT("PsychedelicsRow"));
	// (Stimulation row removed — Stimulation is now a life-meter on the
	// HUD instead of a stat in this panel. StimulationRow UPROPERTY stays
	// as BindWidgetOptional for backward-compat with older WBPs.)

	// (Heat / Thirst / Currency rows + divider intentionally removed —
	// those readouts live on the persistent HUD.)

	// Close button.
	CloseBtn = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CloseBtn"));
	{
		FButtonStyle BS;
		BS.Normal   = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.05f));
		BS.Hovered  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.15f));
		BS.Pressed  = SolidBrush(FLinearColor(0.945f, 0.929f, 0.851f, 0.22f));
		BS.Disabled = SolidBrush(FLinearColor(0.f, 0.f, 0.f, 0.04f));
		CloseBtn->SetStyle(BS);
	}
	UTextBlock* CloseLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CloseBtn_Label"));
	CloseLabel->SetText(FText::FromString(TEXT("CLOSE")));
	CloseLabel->SetFont(MakeBMSPA(18, 4.f));
	CloseLabel->SetColorAndOpacity(FSlateColor(Cream));
	CloseLabel->SetJustification(ETextJustify::Center);
	CloseBtn->SetContent(CloseLabel);
	if (UVerticalBoxSlot* VS = Column->AddChildToVerticalBox(CloseBtn))
	{
		VS->SetPadding(FMargin(0.f, 24.f, 0.f, 0.f));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  Refresh — pulls live values from the GameStateSubsystem onto each row
// ─────────────────────────────────────────────────────────────────────────────

void UEclipseStatsMenuWidget::RefreshAll()
{
	UEclipseGameStateSubsystem* GS = GetGameInstance() ? GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>() : nullptr;
	if (!GS) return;

	auto SetRow = [](UTextBlock* T, const TCHAR* Label, int32 Val)
	{
		if (T) T->SetText(FText::FromString(FString::Printf(TEXT("%s     %d"), Label, Val)));
	};
	SetRow(AestheticsRow,   TEXT("AESTHETICS"),   GS->Aesthetics);
	SetRow(RhythmRow,       TEXT("RHYTHM"),       GS->Rhythm);
	SetRow(ZenRow,          TEXT("ZEN"),          GS->Zen);
	SetRow(PsychedelicsRow, TEXT("PSYCHEDELICS"), GS->Psychedelics);
	// (StimulationRow no longer populated — see header comment.)

	// HEAT / THIRST / CURRENCY intentionally NOT rendered here — those live
	// on the persistent HUD (bottom-right). Stats panel is character stats
	// only. The HeatRow / ThirstRow / CurrencyRow UPROPERTYs remain on the
	// class (BindWidgetOptional, harmless when null) so designer-edited WBPs
	// from earlier revisions don't break linkage; clean them up next time
	// we batch a UCLASS-layout change.
}
