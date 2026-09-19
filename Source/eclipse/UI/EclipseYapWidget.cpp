// Copyright (c) ECLIPSE. All Rights Reserved.

#include "UI/EclipseYapWidget.h"
#include "EclipseUiStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UEclipseYapWidget* UEclipseYapWidget::Show(ACharacter* Speaker, const FString& Line, float HoldSeconds, const FLinearColor& Highlight, float BeatSeconds)
{
	APlayerController* PC = Speaker ? UGameplayStatics::GetPlayerController(Speaker, 0) : nullptr;
	if (!PC) return nullptr;

	TSubclassOf<UEclipseYapWidget> Cls = StaticClass();
	if (UClass* BP = LoadClass<UEclipseYapWidget>(nullptr, TEXT("/Game/Justin/UI/WBP_Yap.WBP_Yap_C"))) Cls = BP;
	UEclipseYapWidget* W = CreateWidget<UEclipseYapWidget>(PC, Cls);
	if (!W) return nullptr;
	W->Speaker = Speaker;
	W->Hold = HoldSeconds;
	W->Beat = BeatSeconds;
	W->SetLine(Line, Highlight);
	W->SetAlignmentInViewport(FVector2D(0.5f, 1.f));
	W->SetRenderTransformPivot(FVector2D(0.5f, 1.f));   // bumps grow up and out from the head
	W->SetRenderOpacity(0.f);
	W->AddToViewport(/*ZOrder=*/350);   // above the dance battle screen, which hosts the battle yaps
	return W;
}

bool UEclipseYapWidget::Initialize()
{
	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
	}
	if (!WidgetTree->FindWidget(FName(TEXT("YapText"))))
	{
		BuildTree(WidgetTree);
	}
	return Super::Initialize();
}

void UEclipseYapWidget::BuildTree(UWidgetTree* Tree)
{
	using namespace EclipseUI;
	auto Text = [Tree](FName Name)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo F = MakeBMSPA(/*Size=*/16, /*LetterSpacingPx=*/1.f);
		F.OutlineSettings.OutlineSize = 2;
		F.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.85f);
		T->SetFont(F);
		T->SetColorAndOpacity(FSlateColor(Cream));
		T->SetShadowOffset(FVector2D(0.f, 2.f));
		T->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f));
		T->SetJustification(ETextJustify::Center);
		T->SetVisibility(ESlateVisibility::HitTestInvisible);
		return T;
	};

	UOverlay* Root = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("YapRoot"));
	Tree->RootWidget = Root;
	UTextBlock* Plain = Text(TEXT("YapText"));
	Plain->SetWrapTextAt(420.f);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Plain)) S->SetHorizontalAlignment(HAlign_Center);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("YapRow"));
	Row->SetVisibility(ESlateVisibility::Collapsed);
	if (UOverlaySlot* S = Root->AddChildToOverlay(Row)) S->SetHorizontalAlignment(HAlign_Center);
	for (const TCHAR* Name : { TEXT("YapBefore"), TEXT("YapWord"), TEXT("YapAfter") })
	{
		UTextBlock* T = Text(Name);
		T->SetRenderTransformPivot(FVector2D(0.5f, 0.6f));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(T)) S->SetVerticalAlignment(VAlign_Bottom);
	}
}

void UEclipseYapWidget::SetLine(const FString& Line, const FLinearColor& Highlight)
{
	HighlightColor = Highlight;
	FString Before, Word, After;
	const bool bMarked = Line.Split(TEXT("_"), &Before, &Word) && Word.Split(TEXT("_"), &Word, &After);
	if (!bMarked || !YapRow || !YapWord)
	{
		if (YapText) YapText->SetText(FText::FromString(Line.Replace(TEXT("_"), TEXT(""))));
		return;
	}
	if (YapText) YapText->SetVisibility(ESlateVisibility::Collapsed);
	YapRow->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (YapBefore) YapBefore->SetText(FText::FromString(Before));
	YapWord->SetText(FText::FromString(Word.ToUpper()));
	YapWord->SetColorAndOpacity(FSlateColor(FMath::Lerp(Highlight, FLinearColor::White, 0.2f)));
	YapWord->SetShadowOffset(FVector2D::ZeroVector);   // the shadow becomes the glow, centred on the word
	if (YapAfter) YapAfter->SetText(FText::FromString(After));
}

void UEclipseYapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	ACharacter* C = Speaker.Get();
	APlayerController* PC = GetOwningPlayer();
	Elapsed += InDeltaTime;
	const float Total = FadeInSeconds + Hold + FadeOutSeconds;
	if (!C || !PC || Elapsed >= Total)
	{
		RemoveFromParent();
		return;
	}

	// On a beat the whole sentence bumps as one; the highlighted word only adds its glow.
	if (Beat > 0.f)
	{
		const float P = 1.f - FMath::Fmod(Elapsed, Beat) / Beat;
		SetRenderScale(FVector2D(1.f + 0.12f * P * P));
		if (YapWord) YapWord->SetShadowColorAndOpacity(HighlightColor.CopyWithNewOpacity(0.9f * P));
	}

	// Just clear of the head, following the character and the camera every frame.
	const FVector Head = C->GetActorLocation() + FVector(0.f, 0.f, C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 25.f);
	FVector2D Screen;
	const bool bOnScreen = PC->ProjectWorldLocationToScreen(Head, Screen, /*bPlayerViewportRelative=*/true);
	const float In = FMath::Clamp(Elapsed / FadeInSeconds, 0.f, 1.f);
	const float Out = FMath::Clamp((Total - Elapsed) / FadeOutSeconds, 0.f, 1.f);
	SetRenderOpacity(bOnScreen ? FMath::Min(In, Out) : 0.f);
	if (bOnScreen) SetPositionInViewport(Screen, /*bRemoveDPIScale=*/true);
}
