// Copyright (c) ECLIPSE. All Rights Reserved.

#include "Subsystems/EclipseDanceBattleSubsystem.h"
#include "eclipse.h"
#include "Data/EclipseDanceTrackData.h"
#include "UI/EclipseDanceBattleWidget.h"
#include "UI/EclipseUiStyle.h"
#include "Player/EclipsePlayerCharacter.h"
#include "NPC/EclipseNpcCharacter.h"
#include "Subsystems/EclipseGameStateSubsystem.h"
#include "Subsystems/EclipseDialogueSubsystem.h"
#include "Subsystems/EclipseAudioSubsystem.h"
#include "Sound/SoundWave.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/EclipseHUDWidget.h"
#include "UI/EclipseInteractWidget.h"

namespace
{
	// ponytail: fixed output-latency guess on top of the playback position; make it a settings slider if PERFECT feels late.
	constexpr double AudioLatencySeconds = 0.04;
	constexpr float MaxSpeedUp = 0.08f;     // top tempo boost for dancing well
	constexpr int32 IntroBars = 4;
	constexpr int32 HeatLostOnDefeat = 2;
	constexpr float CamTiltDegrees = 6.f;
	constexpr float CamOrbitDegrees = 14.f;
	constexpr float CamBumpSeconds = 0.22f;
	constexpr float BaseFOV = 70.f;
	constexpr float SpinUpSeconds = 3.5f;   // vinyl start: pitch ramps from the floor to 1 over this long
	constexpr float PitchFloor = 0.4f;      // the engine's default global minimum pitch
	constexpr float StickDeadzone = 0.6f;
	constexpr int32 DemoBarsPerStyle = 4;
	constexpr int32 LeadBars = 2;             // the opponent calls the next style this many bars ahead
	constexpr float BattleSeconds = 80.f;     // the scored part, after the intro and the demo
	constexpr float StrafeDegPerSec = 18.f;   // a slow sidestep, not a run
	constexpr float StrafeLimitDeg = 90.f;    // either side of where the battle started: a 180-degree arc
	constexpr float FacingWindowDeg = 18.f;   // off by more than this when a switch lands and it's a MISS
	constexpr float HeatModeBars = 8.f;       // how long HEAT mode lasts, in bars
	constexpr float HeatModeSpeedUp = 0.12f;  // extra tempo while it's on
	constexpr int32 HeatAfter = 5;            // HEAT drops back to this when it ends
	constexpr float StanceMax = 150.f;        // the opponent's STANCE; break it and the battle's won early
	constexpr float StanceRecover = 10.f;     // he steadies this much when you MISS

	const TCHAR* GradeNames[] = { TEXT("PERFECT"), TEXT("GOOD"), TEXT("TOO EARLY"), TEXT("TOO SLOW"), TEXT("MISS"), TEXT("HOT") };
	const int32 GradePoints[] = { 300, 100, 25, 50, 0, 500 };
	const float GradeValue[] = { 1.f, 0.7f, 0.25f, 0.35f, 0.f, 1.f };   // feeds Performance
	const float GradeZoom[] = { -14.f, -9.f, 10.f, 0.f, 16.f, -16.f };  // FOV change per grade: good dancing draws the camera right in
	const int32 GradeXP[] = { 25, 10, 0, 0, 0, 30 };                    // style XP for landing a switch
	const int32 GradeHeat[] = { 2, 1, 0, 0, 0, 0 };                     // PERFECT and GOOD build toward HEAT mode
	const float GradeDamage[] = { 20.f, 12.f, 3.f, 5.f, 0.f, 30.f };    // STANCE damage before the style-level multiplier

	// Medal per grade: PERFECT platinum, GOOD gold, TOO SLOW silver, TOO EARLY bronze, MISS red, HOT fire.
	FLinearColor GradeColor(int32 G)
	{
		using namespace EclipseDance;
		const FLinearColor Colors[] = { Platinum, Gold, Bronze, Silver, EclipseUI::DialogueRed, FLinearColor(FColor(0xFF, 0x5A, 0x1F)) };
		return Colors[FMath::Clamp(G, 0, (int32)UE_ARRAY_COUNT(Colors) - 1)];
	}

	// Arrows / D-pad pick styles (held keys matching a combo win, else the key just pressed); the right stick points at the wheel.
	EEclipseDanceStyle StyleFromInput(const APlayerController* PC, EEclipseDanceStyle Current, bool& bStickLatched, int32 Unlocked)
	{
		const auto Open = [Unlocked](int32 i) { return ((Unlocked >> i) & 1) != 0; };
		using namespace EclipseDance;
		static const TTuple<uint8, FKey, FKey> Dirs[] = {
			{ Up, EKeys::Up, EKeys::Gamepad_DPad_Up }, { Left, EKeys::Left, EKeys::Gamepad_DPad_Left },
			{ Down, EKeys::Down, EKeys::Gamepad_DPad_Down }, { Right, EKeys::Right, EKeys::Gamepad_DPad_Right } };
		uint8 Held = 0, Pressed = 0;
		for (const auto& D : Dirs)
		{
			if (PC->IsInputKeyDown(D.Get<1>()) || PC->IsInputKeyDown(D.Get<2>())) Held |= D.Get<0>();
			if (PC->WasInputKeyJustPressed(D.Get<1>()) || PC->WasInputKeyJustPressed(D.Get<2>())) Pressed |= D.Get<0>();
		}
		if (Pressed)
		{
			for (uint8 Mask : { Held, Pressed })
			{
				for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
				{
					if (Open(i) && Info((EEclipseDanceStyle)i).Keys == Mask) return (EEclipseDanceStyle)i;
				}
			}
		}

		// Right stick: flick toward a wheel slot; re-arms once the stick comes back to centre.
		const FVector2D Stick(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX), PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY));
		if (Stick.Size() < StickDeadzone * 0.5f) bStickLatched = false;
		if (bStickLatched || Stick.Size() < StickDeadzone) return Current;
		bStickLatched = true;
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Stick.Y, Stick.X));
		EEclipseDanceStyle Best = Current;
		float BestDist = 30.f;
		for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
		{
			const float Dist = FMath::Abs(FMath::FindDeltaAngleDegrees(Deg, Info((EEclipseDanceStyle)i).WheelDeg));
			if (Open(i) && Dist < BestDist) { BestDist = Dist; Best = (EEclipseDanceStyle)i; }
		}
		return Best;
	}

	// Placeholder moves until the characters are rigged: B is song position in beats.
	void StyleMotion(EEclipseDanceStyle S, float B, FVector& Loc, FRotator& Rot)
	{
		const float Pi = PI;
		const float Bounce = FMath::Abs(FMath::Sin(Pi * B));   // 0 on the beat, 1 between
		switch (S)
		{
		case EEclipseDanceStyle::Liquid:   // gliding side to side
			Loc.Y = 18.f * FMath::Sin(0.5f * Pi * B);
			break;
		case EEclipseDanceStyle::Muzzing:  // slight tilt side to side
			Rot.Roll = 7.f * FMath::Sin(Pi * B);
			break;
		case EEclipseDanceStyle::Gloving:  // jumping up and down on every beat
			Loc.Z = 14.f * Bounce;
			break;
		case EEclipseDanceStyle::Hakken:   // stamping down into the floor on the beat
			Loc.Z = -10.f * (1.f - Bounce);
			break;
		case EEclipseDanceStyle::Tektonik: // small hop with a tiny sway
			Loc.Z = 6.f * Bounce;
			Loc.Y = 4.f * FMath::Sin(Pi * B);
			break;
		default:                           // not dancing yet: nodding along
			Loc.Z = 2.f * Bounce;
			break;
		}
	}
}

bool UEclipseDanceBattleSubsystem::StartBattle(UEclipseDanceTrackData* InTrack, AEclipseNpcCharacter* InOpponent)
{
	if (!InTrack || !InTrack->Sound) return false;
	Stop();

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) return false;

	Track = InTrack;
	Opponent = InOpponent;
	Unlocked = ~0;
	if (UEclipseGameStateSubsystem* GS = World->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>()) Unlocked = GS->UnlockedDanceStyles;
	BuildSchedule();
	LastBar = -1;
	Score = OpponentScore = 0;
	GradedSwitches = 0;
	FMemory::Memzero(Counts);
	Performance = 0.5f;
	bFinished = false;
	PlayerStyle = OpponentStyle = EEclipseDanceStyle::Count;
	StyleChosenAt = 0.0;
	HaloFlash = BeatFlash = 0.f;
	BeatIndex = -1;
	SongClock = InTrack->SegmentStartSeconds();
	bWarnedPlayback = false;
	Pitch = PitchFloor;
	WinT = -1.f;
	Wonk = CamTime = 0.f;
	CamFOV = CamFOVTarget = BaseFOV;
	bHeatMode = false;
	CamTimeDrift = 0.f;
	OpponentStance = StanceMax;
	bStickLatched = false;
	SpinT = -1.f;

	// Proficiency 1-3 per style and the intro barks both live in Ink, keyed by the opponent's knot.
	IntroLines.Reset();
	DemoLine.Reset();
	for (TArray<FString>& L : LeadLines) L.Reset();
	for (TArray<FString>& L : ShowLines) L.Reset();
	for (int32& L : OpponentLevel) L = 1;
	if (UEclipseDialogueSubsystem* DS = World->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>(); DS && Opponent)
	{
		const FString Id = Opponent->DialogueId.ToString();
		for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
		{
			const FString Var = FString::Printf(TEXT("%s_%s"), *Id, *FString(EclipseDance::Info((EEclipseDanceStyle)i).Name).ToLower());
			OpponentLevel[i] = FMath::Clamp(DS->GetInkInt(Var, 1), 0, 3);
		}
		IntroLines = DS->ReadKnotLines(Id + TEXT("_battle_intro"));
		const TArray<FString> Demo = DS->ReadKnotLines(Id + TEXT("_battle_demo"));
		DemoLine = Demo.Num() ? Demo[0] : TEXT("Follow my style.");
		for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
		{
			const FString Style = FString(EclipseDance::Info((EEclipseDanceStyle)i).Name).ToLower();
			LeadLines[i] = DS->ReadKnotLines(FString::Printf(TEXT("%s_lead_%s"), *Id, *Style));
			ShowLines[i] = DS->ReadKnotLines(FString::Printf(TEXT("%s_show_%s"), *Id, *Style));
		}
	}

	// Everything keys off where the song actually is, so the spin-up, speed-ups and pausing can't drift it off the grid.
	Music = UGameplayStatics::CreateSound2D(this, InTrack->Sound, 1.f, PitchFloor, 0.f, nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
	if (!Music) { Stop(); return false; }
	Music->bIsUISound = false;   // so the pause menu pauses it
	Music->ComponentTags.Add(UEclipseAudioSubsystem::DeckManagedTag);   // sets its own pitch, deck speed folded in
	Music->OnAudioPlaybackPercentNative.AddUObject(this, &UEclipseDanceBattleSubsystem::HandlePlaybackPercent);
	Music->Play(InTrack->SegmentStartSeconds());
	SpinT = 0.f;

	TSubclassOf<UEclipseDanceBattleWidget> WidgetClass = UEclipseDanceBattleWidget::StaticClass();
	if (UClass* BP = LoadClass<UEclipseDanceBattleWidget>(nullptr, TEXT("/Game/Justin/UI/WBP_DanceBattle.WBP_DanceBattle_C")))
	{
		WidgetClass = BP;
	}
	Widget = CreateWidget<UEclipseDanceBattleWidget>(PC, WidgetClass);
	if (Widget)
	{
		Widget->AddToViewport(/*ZOrder=*/300);
		Widget->ShowStyle(EEclipseDanceStyle::Count);
		Widget->SetRadialSelected(PlayerStyle);
		Widget->SetUnlockedStyles(Unlocked);
		Widget->OnContinue.BindUObject(this, &UEclipseDanceBattleSubsystem::Stop);
	}
	EnterBattleCamera();
	SpawnWaveWall();
	SetGameUiHidden(true);

	UE_LOG(LogEclipse, Log, TEXT("Dance battle vs %s: %.2f BPM, %d bars from %.2fs"),
		*GetNameSafe(InOpponent), InTrack->BPM, Schedule.Num(), InTrack->SegmentStartSeconds());
	return true;
}

void UEclipseDanceBattleSubsystem::BuildSchedule()
{
	// Seeded by opponent so a retry dances the same routine.
	FRandomStream Rng(Opponent ? GetTypeHash(Opponent->GetName()) : 7);

	TArray<EEclipseDanceStyle> Pool;
	for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i) if ((Unlocked >> i) & 1) Pool.Add((EEclipseDanceStyle)i);
	if (Pool.Num() < 2) Pool = { EEclipseDanceStyle::Muzzing, EEclipseDanceStyle::Tektonik };

	Schedule.Init(EEclipseDanceStyle::Count, IntroBars);
	for (EEclipseDanceStyle S : Pool)
	{
		for (int32 b = 0; b < DemoBarsPerStyle; ++b) Schedule.Add(S);
	}
	FirstGraded = Schedule.Num();
	const int32 NumBars = FirstGraded + FMath::RoundToInt(BattleSeconds / Track->BarSeconds());
	EEclipseDanceStyle Current = Schedule.Last();
	while (Current == Schedule.Last()) Current = Pool[Rng.RandRange(0, Pool.Num() - 1)];
	while (Schedule.Num() < NumBars)
	{
		const int32 Run = Rng.FRand() < 0.5f ? 4 : 8;
		for (int32 b = 0; b < Run && Schedule.Num() < NumBars; ++b) Schedule.Add(Current);
		EEclipseDanceStyle Next = Current;
		while (Next == Current) Next = Pool[Rng.RandRange(0, Pool.Num() - 1)];
		Current = Next;
	}
}

void UEclipseDanceBattleSubsystem::SpawnWaveWall()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!Opponent || !PC) return;

	// A translucent panel just behind the opponent, square to the camera, so the dancers stand in front of the wave.
	const FVector Away = (-CamOffset).GetSafeNormal2D();
	const FVector Loc = Opponent->GetActorLocation() + Away * 140.f + FVector(0.f, 0.f, CamMid.Z - Opponent->GetActorLocation().Z);
	WaveWall = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform((-Away).Rotation(), Loc));
	if (!WaveWall) return;
	UWidgetComponent* Panel = NewObject<UWidgetComponent>(WaveWall, TEXT("WavePanel"));
	WaveWall->SetRootComponent(Panel);
	Panel->SetWidgetSpace(EWidgetSpace::World);
	Panel->SetWidgetClass(UEclipseWaveformWidget::StaticClass());
	Panel->SetDrawSize(FVector2D(2000.f, 560.f));
	Panel->SetWorldScale3D(FVector(0.5f));
	Panel->SetTwoSided(true);
	Panel->SetBlendMode(EWidgetBlendMode::Transparent);
	Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panel->SetWorldLocationAndRotation(Loc, (-Away).Rotation());
	Panel->RegisterComponent();
	// Overlays the room (no depth test) but cuts out wherever a dancer is in front, via their custom depth.
	if (UMaterialInterface* Overlay = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Justin/Materials/M_WaveOverlay.M_WaveOverlay")))
	{
		Panel->SetMaterial(0, Overlay);
	}
	Panel->InitWidget();
	Wave = Cast<UEclipseWaveformWidget>(Panel->GetUserWidgetObject());
	if (!Wave) return;

	TArray<TPair<float, EEclipseDanceStyle>> Switches;
	for (int32 Bar = 1; Bar < Schedule.Num(); ++Bar)
	{
		if (Schedule[Bar] != Schedule[Bar - 1]) Switches.Emplace(Track->SegmentStartSeconds() + Bar * Track->BarSeconds(), Schedule[Bar]);
	}
	Wave->SetTrack(Track, Switches);
	Wave->SetPlayhead(Track->SegmentStartSeconds());
}

void UEclipseDanceBattleSubsystem::SetGameUiHidden(bool bHidden)
{
	if (!bHidden)
	{
		for (const TWeakObjectPtr<UUserWidget>& W : HiddenUi) if (W.IsValid()) W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		HiddenUi.Reset();
		return;
	}
	// HEAT, THIRST, the clock and the interact prompts all step aside for the battle.
	for (UClass* Cls : { UEclipseHUDWidget::StaticClass(), UEclipseInteractWidget::StaticClass() })
	{
		TArray<UUserWidget*> Found;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, Cls, /*TopLevelOnly=*/true);
		for (UUserWidget* W : Found)
		{
			if (!W->IsVisible()) continue;
			W->SetVisibility(ESlateVisibility::Collapsed);
			HiddenUi.Add(W);
		}
	}
}

void UEclipseDanceBattleSubsystem::EnterBattleCamera()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	ACharacter* Player = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	if (!Player || !Opponent) return;

	const FVector From = Player->GetActorLocation();
	const FVector To = Opponent->GetActorLocation();
	const FVector Dir = (To - From).GetSafeNormal2D();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir);

	// Dialogue-style two-shot: behind and to one side of the player, framing both dancers.
	CamMid = (From + To) * 0.5f + FVector(0.f, 0.f, 30.f);
	CamOffset = (From - Dir * 280.f + Right * 220.f + FVector(0.f, 0.f, 70.f)) - CamMid;
	CamRoll = CamRollTarget = CamOrbit = CamOrbitTarget = 0.f;
	CamBump = 1.f;
	BattleCamera = World->SpawnActor<ACameraActor>(CamMid + CamOffset, (-CamOffset).Rotation());
	if (BattleCamera)
	{
		BattleCamera->GetCameraComponent()->SetFieldOfView(BaseFOV);
		PC->SetViewTargetWithBlend(BattleCamera, 0.6f, VTBlend_Cubic);
	}

	// Coloured wash over the floor between the dancers, flashed on every beat.
	StageLight = World->SpawnActor<APointLight>(CamMid + FVector(0.f, 0.f, 220.f), FRotator::ZeroRotator);
	if (StageLight)
	{
		UPointLightComponent* L = StageLight->PointLightComponent;
		L->SetMobility(EComponentMobility::Movable);
		L->SetIntensityUnits(ELightUnits::Candelas);
		L->SetAttenuationRadius(900.f);
		L->SetCastShadows(false);
		L->SetIntensity(0.f);
	}

	// Halo that flares when the player changes style and settles to a soft glow while dancing.
	PlayerHalo = NewObject<UPointLightComponent>(Player);
	PlayerHalo->SetMobility(EComponentMobility::Movable);
	PlayerHalo->SetupAttachment(Player->GetRootComponent());
	PlayerHalo->SetRelativeLocation(FVector(0.f, 0.f, 30.f));
	PlayerHalo->SetIntensityUnits(ELightUnits::Candelas);
	PlayerHalo->SetAttenuationRadius(220.f);
	PlayerHalo->SetCastShadows(false);
	PlayerHalo->SetIntensity(0.f);
	PlayerHalo->RegisterComponent();

	// You and the opponent face off across a circle; you always face its centre, so you're only facing them when you're opposite.
	StrafePivot = (From + To) * 0.5f;
	const FVector FromPivot = (From - StrafePivot).GetSafeNormal2D();
	StrafeCentreDeg = FMath::RadiansToDegrees(FMath::Atan2(FromPivot.Y, FromPivot.X));
	StrafeDeg = OpponentDrift = FacingError = 0.f;
	StrafeRadius = FMath::Clamp(FVector::Dist2D(From, To) * 0.5f, 110.f, 180.f);
	if (AEclipsePlayerCharacter* EP = Cast<AEclipsePlayerCharacter>(Player)) EP->StartFaceTarget(StrafePivot);
	Opponent->StartFacePlayer(Player);
	PC->SetIgnoreMoveInput(true);   // free walking is off; A/D strafe round the circle (see TickStrafe)

	MeshBase.Reset();
	for (ACharacter* C : { Player, static_cast<ACharacter*>(Opponent.Get()) })
	{
		if (!C || !C->GetMesh()) continue;
		MeshBase.Add(C, C->GetMesh()->GetRelativeTransform());
		C->GetMesh()->SetRenderCustomDepth(true);   // lets the wave overlay cut around them
	}
}

void UEclipseDanceBattleSubsystem::LeaveBattleCamera()
{
	for (const TPair<TWeakObjectPtr<ACharacter>, FTransform>& It : MeshBase)
	{
		if (ACharacter* C = It.Key.Get())
		{
			C->GetMesh()->SetRelativeTransform(It.Value);
			C->GetMesh()->SetRenderCustomDepth(false);
		}
	}
	MeshBase.Reset();

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (PC)
	{
		if (APawn* Player = PC->GetPawn())
		{
			PC->SetViewTargetWithBlend(Player, 0.6f, VTBlend_Cubic);
			if (AEclipsePlayerCharacter* EP = Cast<AEclipsePlayerCharacter>(Player)) EP->StopFaceTarget();
		}
		PC->SetIgnoreMoveInput(false);
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
	if (Opponent) Opponent->StopFacePlayer();
	if (BattleCamera) BattleCamera->SetLifeSpan(1.f);   // outlive the blend back
	BattleCamera = nullptr;
	if (StageLight) StageLight->Destroy();
	StageLight = nullptr;
	if (PlayerHalo) PlayerHalo->DestroyComponent();
	PlayerHalo = nullptr;
	if (WaveWall) WaveWall->Destroy();
	WaveWall = nullptr;
	Wave = nullptr;
	SetGameUiHidden(false);
}

void UEclipseDanceBattleSubsystem::Stop()
{
	if (!Track) return;
	if (Music)
	{
		Music->FadeOut(0.4f, 0.f);
		Music = nullptr;
	}
	if (Widget)
	{
		Widget->RemoveFromParent();
		Widget = nullptr;
	}
	LeaveBattleCamera();
	Track = nullptr;
	Opponent = nullptr;
}

void UEclipseDanceBattleSubsystem::HandleBeat(int32 Bar, int32 Beat)
{
	if (!Track || bFinished) return;

	if (Bar >= Schedule.Num())
	{
		Finish();
		return;
	}

	LastBar = Bar;

	// Intro barks, spread evenly across the intro bars and held until the next one.
	// Real seconds, not song seconds: the tempo can be pushed up.
	const float BeatSec = Track->BeatSeconds() / Pitch;
	const float BarHold = Track->BarSeconds() / Pitch - 0.2f;
	if (Beat == 1 && Bar < IntroBars - 1 && Opponent && IntroLines.Num() > 0)
	{
		const int32 Stride = FMath::Max(1, (IntroBars - 1) / IntroLines.Num());
		if (Bar % Stride == 0 && IntroLines.IsValidIndex(Bar / Stride))
		{
			Opponent->Yap(IntroLines[Bar / Stride], Stride * Track->BarSeconds() - 0.2f, FLinearColor::White, BeatSec);
		}
	}

	const EEclipseDanceStyle Style = Schedule[Bar];
	const bool bDemoBar = Bar >= IntroBars && Bar < FirstGraded;
	const auto BlockStart = [&](int32 B) { return B >= IntroBars && B < FirstGraded && (B - IntroBars) % DemoBarsPerStyle == 0; };
	const bool bSwitchBar = Bar >= FirstGraded && Style != Schedule[Bar - 1];

	// Says a style's line with the style word lit in its colour, bumping on the beat.
	const auto Say = [&](const TArray<FString>& Lines, EEclipseDanceStyle S, float Hold)
	{
		if (!Opponent) return;
		const FString Line = Lines.Num() ? Lines[FMath::RandRange(0, Lines.Num() - 1)]
			: FString::Printf(TEXT("_%s_ - %s."), *FString(EclipseDance::Info(S).Name).ToLower(), EclipseDance::Info(S).KeyHint);
		Opponent->Yap(Line, Hold, EclipseDance::StyleColor(S), BeatSec);
	};

	// Last intro bar: "Follow my style." Then he dances each unlocked style for four bars, naming it, before anything counts.
	if (Beat == 1 && Bar == IntroBars - 1 && Opponent) Opponent->Yap(DemoLine, BarHold, FLinearColor::White, BeatSec);
	if (bDemoBar && Beat == 1)
	{
		OpponentStyle = Style;
		if (Widget) Widget->ShowStyle(Style);
		if (BlockStart(Bar)) Say(ShowLines[(int32)Style], Style, BarHold * 2.f);
	}
	// Four bars ahead of every graded switch. A demo block's first bar belongs to its own line, so a lead due then waits a bar.
	for (const int32 Target : { BlockStart(Bar) ? -1 : Bar + LeadBars, BlockStart(Bar - 1) ? Bar + LeadBars - 1 : -1 })
	{
		if (Beat == 1 && Target >= FirstGraded && Schedule.IsValidIndex(Target) && Schedule[Target] != Schedule[Target - 1])
		{
			Say(LeadLines[(int32)Schedule[Target]], Schedule[Target], BarHold * 2.f);
		}
	}

	if (bSwitchBar && Beat == 1)
	{
		SwitchTime = Track->SegmentStartSeconds() + Bar * Track->BarSeconds();   // song time of the downbeat
		RollOpponentSwitch(Bar);
		if (Widget) Widget->ShowStyle(Style);
		SwitchPump = 1.f;
		CamRollTarget = CamRollTarget > 0.f ? -CamTiltDegrees : CamTiltDegrees;
		CamOrbitTarget = CamOrbitTarget > 0.f ? -CamOrbitDegrees : CamOrbitDegrees;
		CamBump = 0.f;
	}
	// Grade a beat after the switch so a slightly late press still counts as TOO SLOW rather than a miss.
	if (bSwitchBar && Beat == 2) GradeSwitch(Bar);

	if (!Widget) return;
	// The bar before a switch counts it in on its own beats: 4, 3, 2, 1.
	const bool bSwitchNext = Bar + 1 >= FirstGraded && Schedule.IsValidIndex(Bar + 1) && Schedule[Bar + 1] != Style;
	Widget->ShowCountdown(bSwitchNext ? Track->BeatsPerBar + 1 - Beat : 0,
		bSwitchNext ? Schedule[Bar + 1] : Style, BeatSec);
}

void UEclipseDanceBattleSubsystem::RollOpponentSwitch(int32 Bar)
{
	// Level 1 lands ~60% of switches, 2 ~75%, 3 ~90%; a miss keeps them dancing the old style.
	const int32 Level = OpponentLevel[(int32)Schedule[Bar]];
	bOpponentHit = FMath::FRand() < FMath::Clamp(0.45f + 0.15f * Level, 0.f, 0.95f);
	if (bOpponentHit) OpponentStyle = Schedule[Bar];
}

void UEclipseDanceBattleSubsystem::GradeSwitch(int32 Bar)
{
	const double Dt = StyleChosenAt - SwitchTime;
	const double Beat = Track->BeatSeconds();

	// For Honor-style: any switch inside the colour blend (a bar ahead) is GOOD, right on the switch is PERFECT.
	// Facing away from the opponent when it lands throws the whole thing: MISS.
	const double D = Dt - EclipseDance::JudgeOffset;
	// A levelled style is easier to land: its PERFECT window widens 10% per level.
	UEclipseGameStateSubsystem* StyleGS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>();
	const int32 StyleLevel = StyleGS && StyleGS->DanceStyleLevels.IsValidIndex((int32)Schedule[Bar]) ? StyleGS->DanceStyleLevels[(int32)Schedule[Bar]] : 1;
	const double PerfectHalf = EclipseDance::PerfectHalf * (1.0 + 0.1 * (StyleLevel - 1));
	EGrade G;
	if (PlayerStyle != Schedule[Bar] || FacingError > FacingWindowDeg) G = EGrade::Miss;
	else if (D < -EclipseDance::SwitchWindowBeats * Beat)                G = EGrade::TooEarly;
	else if (FMath::Abs(D) <= PerfectHalf)                               G = EGrade::Perfect;
	else if (D <= EclipseDance::GoodLate)                               G = EGrade::Good;
	else                                                                G = EGrade::TooSlow;
	// In HEAT mode every landed switch burns HOT.
	if (bHeatMode && (G == EGrade::Perfect || G == EGrade::Good)) G = EGrade::Hot;

	++Counts[(int32)G];
	if (G == EGrade::Miss) Wonk = FMath::Min(Wonk + 1.f, 2.f);
	Score += GradePoints[(int32)G];
	CamFOVTarget = FMath::Clamp(CamFOVTarget + GradeZoom[(int32)G], 34.f, 100.f);
	if (GradeXP[(int32)G] > 0)
	{
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>()) GS->AddDanceStyleXP(Schedule[Bar], GradeXP[(int32)G]);
	}
	if (GradeHeat[(int32)G] > 0)
	{
		// Capped at the top rather than overflowing (which would overheat you); hitting the top lights HEAT mode.
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->ChangeMeter(TEXT("heat"), FMath::Min(GradeHeat[(int32)G], UEclipseGameStateSubsystem::MeterMax - GS->Heat));
			if (GS->Heat >= UEclipseGameStateSubsystem::MeterMax && !bHeatMode)
			{
				bHeatMode = true;
				HeatModeEnds = Track->SegmentStartSeconds() + (Bar + HeatModeBars) * Track->BarSeconds();
				CamBump = 0.f;
				UE_LOG(LogEclipse, Log, TEXT("Dance: HEAT mode on until bar %d"), Bar + (int32)HeatModeBars);
			}
		}
	}
	Performance = FMath::Lerp(Performance, GradeValue[(int32)G], 0.4f);

	if (bOpponentHit)
	{
		const int32 Level = OpponentLevel[(int32)Schedule[Bar]];
		OpponentScore += FMath::FRand() < 0.12f * Level ? GradePoints[(int32)EGrade::Perfect] : GradePoints[(int32)EGrade::Good];
	}

	// Knock his STANCE down: harder with a levelled style, harder still in HEAT mode; a MISS lets him steady himself.
	const float Damage = GradeDamage[(int32)G] * (1.f + 0.25f * (StyleLevel - 1)) * (bHeatMode ? 1.5f : 1.f);
	OpponentStance = FMath::Clamp(OpponentStance - Damage + (G == EGrade::Miss ? StanceRecover : 0.f), 0.f, StanceMax);
	if (Widget) Widget->SetStance(OpponentStance / StanceMax, Damage > 0.f);

	++GradedSwitches;
	if (Widget) Widget->ShowGrade(FText::FromString(GradeNames[(int32)G]), GradeColor((int32)G));
	if (OpponentStance <= 0.f)
	{
		UE_LOG(LogEclipse, Log, TEXT("Dance: STANCE BROKEN at bar %d"), Bar);
		Finish();
		return;
	}
	UE_LOG(LogEclipse, Log, TEXT("Dance switch bar %d: %s (%+.0f ms, facing off %.0f deg) score %d vs %d"), Bar, GradeNames[(int32)G], Dt * 1000.0, FacingError, Score, OpponentScore);
}

void UEclipseDanceBattleSubsystem::Finish()
{
	bFinished = true;

	const bool bStanceBroken = OpponentStance <= 0.f;
	const bool bWon = bStanceBroken || Score >= OpponentScore;
	// Win: the track rides out. Loss: the deck brakes, stutters on a scrap of sound and dies.
	if (bWon) WinT = 0.f;
	if (!bWon)
	{
		// Where the braking deck will have got to, so the stutter repeats what was last heard.
		FailLoopAt = SongSeconds(FPlatformTime::Seconds()) + 1.4f * (1.f + PitchFloor) * 0.5f;
		FailStep = -1;
		FailT = 0.f;
	}
	if (!bWon)
	{
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->ChangeMeter(TEXT("heat"), -HeatLostOnDefeat);
		}
	}

	if (Widget)
	{
		Widget->ShowCountdown(0, EEclipseDanceStyle::Count, 0.f);
		// Scores are out of 1000, a perfect run; the rating and its medal colour come from the same fraction.
		const float Best = FMath::Max(1, GradedSwitches * GradePoints[(int32)EGrade::Perfect]);
		const auto Row = [Best](const FString& Label, int32 Points, FString* Rating = nullptr)
		{
			const float Ratio = FMath::Min(1.f, Points / Best);   // HOT hits can overshoot a perfect run; 1000 is the ceiling
			FEclipseDanceResultRow R{ Label, FMath::RoundToInt(1000.f * Ratio) };
			FString Name;
			EclipseDance::Tier(Ratio, Name, R.Color, R.bGlow);
			if (Rating) *Rating = Name;
			return R;
		};
		TArray<FEclipseDanceResultRow> Rows;
		for (int32 i = 0; i < (int32)EGrade::Count; ++i) Rows.Add({ GradeNames[i], Counts[i], FString(), GradeColor(i), i == (int32)EGrade::Perfect });
		FString Rating;
		const FEclipseDanceResultRow Mine = Row(TEXT("SCORE"), Score, &Rating);
		Rows.Add(Mine);
		Rows.Add({ TEXT("RATING"), 0, Rating, Mine.Color, Mine.bGlow });
		Rows.Add(Row(Opponent ? Opponent->GetDisplayName().ToString().ToUpper() : TEXT("RIVAL"), OpponentScore));
		const FString Verdict = bStanceBroken ? TEXT("STANCE BROKEN") : bWon ? TEXT("YOU WIN") : FString::Printf(TEXT("YOU LOSE   HEAT -%d"), HeatLostOnDefeat);
		Widget->ShowResults(Rows, Verdict, bWon ? Mine.Color : EclipseUI::DialogueRed, bWon && Mine.bGlow);
	}
	UE_LOG(LogEclipse, Log, TEXT("Dance battle over: %s, %d vs %d"), bWon ? TEXT("won") : TEXT("lost"), Score, OpponentScore);

	// The board stays up until CONTINUE, which needs the cursor.
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}
}

void UEclipseDanceBattleSubsystem::HandlePlaybackPercent(const UAudioComponent* Comp, const USoundWave* PlayingWave, const float Percent)
{
	// The file's true length comes from the baked envelope; a looping wave's own duration can't be trusted at runtime.
	if (!Track || Track->Envelope.Num() == 0) return;
	const float Reported = Percent * Track->Envelope.Num() / Track->EnvelopeRate;
	// Only a small nudge toward what the audio thread says; a wild report is ignored rather than jumping the battle.
	const float Error = Reported - SongClock;
	if (FMath::Abs(Error) < 0.5f) SongClock += Error * 0.2f;
	else if (!bWarnedPlayback) { bWarnedPlayback = true; UE_LOG(LogEclipse, Warning, TEXT("Dance: ignoring playback report %.2fs (clock %.2fs)"), Reported, SongClock); }
}

float UEclipseDanceBattleSubsystem::DeckSpeed() const
{
	const UEclipseAudioSubsystem* Audio = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UEclipseAudioSubsystem>() : nullptr;
	return Audio ? Audio->GetDeckSpeed() : 1.f;
}

float UEclipseDanceBattleSubsystem::SongSeconds(double Now) const
{
	return SongClock - AudioLatencySeconds;
}

void UEclipseDanceBattleSubsystem::Tick(float DeltaTime)
{
	if (!Track) return;
	const double Now = FPlatformTime::Seconds();

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && bFinished)
	{
		for (const FKey& K : { EKeys::E, EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom })
		{
			if (PC->WasInputKeyJustPressed(K)) { Stop(); return; }
		}
	}
	if (PC && !bFinished) TickStrafe(PC, DeltaTime);
	if (PC && !bFinished && LastBar >= 0)
	{
		const EEclipseDanceStyle Picked = StyleFromInput(PC, PlayerStyle, bStickLatched, Unlocked);
		if (Picked != PlayerStyle)
		{
			PlayerStyle = Picked;
			StyleChosenAt = SongSeconds(Now);
			HaloFlash = 1.f;
			if (Widget)
			{
				Widget->SetRadialSelected(Picked);
				Widget->Pulse(EclipseDance::StyleColor(Picked), 0.35f, 1.6f);   // confirms the pick
			}
		}
	}

	// The song moves at the speed it's actually playing; world ticks stop while paused, and so does the song.
	if (Music && Music->IsPlaying()) SongClock += DeltaTime * FMath::Max(PitchFloor, Pitch * DeckSpeed());
	const float Song = SongSeconds(Now);

	// Fire each beat as the song reaches it (catching up if a frame skipped one).
	const float Beats = (Song - Track->SegmentStartSeconds()) / Track->BeatSeconds();
	BeatFlash = FMath::Square(1.f - FMath::Frac(FMath::Max(0.f, Beats)));   // 1 on the beat, easing off across it
	// Backbeat: the lights swell through 1 and 3 and peak on 2 and 4, then drop away.
	const float Pair = FMath::Fmod(FMath::Max(0.f, Beats), 2.f);
	Backbeat = Pair < 1.f ? Pair * Pair * Pair : FMath::Exp(-(Pair - 1.f) * 5.f);
	if (Widget)
	{
		const EEclipseDanceStyle Playing = LastBar >= 0 ? Schedule[FMath::Clamp(LastBar, 0, Schedule.Num() - 1)] : EEclipseDanceStyle::Count;
		Widget->SetBackbeat(Playing == EEclipseDanceStyle::Count ? FLinearColor(0.8f, 0.8f, 1.f) : EclipseDance::StyleColor(Playing), bFinished ? 0.f : Backbeat);
	}
	while (!bFinished && Track && BeatIndex < FMath::FloorToInt(Beats))
	{
		++BeatIndex;
		HandleBeat(BeatIndex / Track->BeatsPerBar, BeatIndex % Track->BeatsPerBar + 1);
	}
	if (!Track) return;   // a beat can end the battle
	if (bHeatMode && (Song >= HeatModeEnds || bFinished))
	{
		bHeatMode = false;
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>()) GS->ChangeMeter(TEXT("heat"), HeatAfter - GS->Heat);
	}
	if (Widget)
	{
		const UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>();
		Widget->SetHeat(GS ? GS->Heat : 0, bHeatMode);
	}
	if (Wave) Wave->SetHot(bHeatMode);
	if (Wave)
	{
		const EEclipseDanceStyle S = LastBar >= 0 ? Schedule[FMath::Clamp(LastBar, 0, Schedule.Num() - 1)] : EEclipseDanceStyle::Count;
		Wave->SetPlayhead(Song);
		Wave->SetPump(BeatFlash + 1.6f * SwitchPump);
		// The wave comes up with the record: flat and faint at the floor pitch, full once it's at speed.
		Wave->SetPresence(FMath::Clamp((FMath::Min(Pitch, 1.f) - PitchFloor) / (1.f - PitchFloor), 0.f, 1.f));
	}
	TickMusic(DeltaTime);
	TickCamera(DeltaTime);
	TickDancers(Now, DeltaTime);
}

void UEclipseDanceBattleSubsystem::TickCamera(float DeltaTime)
{
	if (!BattleCamera) return;
	CamTime += DeltaTime;

	// The player can circle the opponent, so the two-shot re-frames around wherever they both are now.
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (APawn* Player = PC ? PC->GetPawn() : nullptr; Player && Opponent)
	{
		const FVector From = Player->GetActorLocation();
		const FVector To = Opponent->GetActorLocation();
		const FVector Dir = (To - From).GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Dir);
		const FVector Mid = (From + To) * 0.5f + FVector(0.f, 0.f, 30.f);
		CamMid = FMath::VInterpTo(CamMid, Mid, DeltaTime, 3.f);
		CamOffset = FMath::VInterpTo(CamOffset, (From - Dir * 280.f + Right * 220.f + FVector(0.f, 0.f, 70.f)) - Mid, DeltaTime, 3.f);

	}
	CamRoll = FMath::FInterpTo(CamRoll, CamRollTarget, DeltaTime, 8.f);
	CamOrbit = FMath::FInterpTo(CamOrbit, CamOrbitTarget, DeltaTime, 5.f);
	CamBump = FMath::Min(1.f, CamBump + DeltaTime / CamBumpSeconds);
	Wonk = FMath::Max(0.f, Wonk - DeltaTime * 0.35f);
	// Dancing well draws the camera in; slipping pulls it back out.
	CamFOV = FMath::FInterpTo(CamFOV, CamFOVTarget, DeltaTime, 4.f);

	// 0 → 1 → 0: a quick punch. Dancing well punches in hard; dancing badly pulls out instead.
	const float Bump = FMath::Sin(PI * CamBump);
	const float Punch = FMath::Lerp(-0.8f, 1.8f, Performance) * Bump;
	// Misses leave the camera drunk for a while: a wobbling dutch angle and a drifting orbit.
	const float Drunk = Wonk * Wonk;
	const float Orbit = CamOrbit + Drunk * 6.f * FMath::Sin(CamTime * 1.7f);
	const FVector Loc = CamMid + CamOffset.RotateAngleAxis(Orbit, FVector::UpVector) + FVector(0.f, 0.f, Drunk * 12.f * FMath::Sin(CamTime * 2.9f));
	FRotator Rot = (CamMid - Loc).Rotation();
	Rot.Roll = CamRoll + Drunk * 9.f * FMath::Sin(CamTime * 2.3f);
	BattleCamera->SetActorLocationAndRotation(Loc + Rot.Vector() * 30.f * Punch, Rot);
	BattleCamera->GetCameraComponent()->SetFieldOfView(CamFOV - 7.f * Punch);

	// The wave panel stays square to the camera, just behind the opponent.
	if (WaveWall && Opponent)
	{
		const FVector Away = (Opponent->GetActorLocation() - Loc).GetSafeNormal2D();
		FVector WallLoc = Opponent->GetActorLocation() + Away * 140.f;
		WallLoc.Z = CamMid.Z;
		WaveWall->SetActorLocationAndRotation(WallLoc, (-Away).Rotation());
	}
}

void UEclipseDanceBattleSubsystem::TickStrafe(APlayerController* PC, float DeltaTime)
{
	APawn* Player = PC->GetPawn();
	if (!Player || !Opponent) return;
	const auto OnCircle = [this](float Deg, float Z)
	{
		const float Rad = FMath::DegreesToRadians(Deg);
		return FVector(StrafePivot.X + FMath::Cos(Rad) * StrafeRadius, StrafePivot.Y + FMath::Sin(Rad) * StrafeRadius, Z);
	};

	// The opponent drifts slowly round the circle, wandering rather than sweeping, once the scored battle's close.
	if (LastBar >= FirstGraded - 1)
	{
		CamTimeDrift += DeltaTime;
		OpponentDrift = 55.f * FMath::Sin(CamTimeDrift * 0.21f) + 22.f * (FMath::Sin(CamTimeDrift * 0.53f + 1.3f) - FMath::Sin(1.3f));   // starts from 0
	}
	Opponent->SetActorLocation(OnCircle(StrafeCentreDeg + 180.f + OpponentDrift, Opponent->GetActorLocation().Z));

	// A/D or the left stick walk you round your side of the circle, never more than a quarter turn either way.
	float Axis = PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX);
	if (PC->IsInputKeyDown(EKeys::D)) Axis += 1.f;
	if (PC->IsInputKeyDown(EKeys::A)) Axis -= 1.f;
	Axis = FMath::Clamp(Axis, -1.f, 1.f);
	if (FMath::Abs(Axis) >= 0.15f) StrafeDeg = FMath::Clamp(StrafeDeg - Axis * StrafeDegPerSec * DeltaTime, -StrafeLimitDeg, StrafeLimitDeg);
	Player->SetActorLocation(OnCircle(StrafeCentreDeg + StrafeDeg, Player->GetActorLocation().Z), /*bSweep=*/true);

	// Facing him means sitting directly opposite: your angle and his drift match.
	const float Signed = FMath::FindDeltaAngleDegrees(StrafeDeg, OpponentDrift);
	FacingError = FMath::Abs(Signed);
	if (Widget) Widget->SetFacing(Signed, FacingWindowDeg);
}

void UEclipseDanceBattleSubsystem::TickMusic(float DeltaTime)
{
	if (!Music) return;
	if (SpinT >= 0.f)
	{
		// Vinyl start, then the tempo creeps up while you're dancing well and settles back when you're not.
		SpinT = FMath::Min(SpinUpSeconds, SpinT + DeltaTime);
		const float Cruise = 1.f + MaxSpeedUp * FMath::Clamp((Performance - 0.55f) / 0.4f, 0.f, 1.f) + (bHeatMode ? HeatModeSpeedUp : 0.f);
		Pitch = SpinT < SpinUpSeconds ? FMath::Lerp(PitchFloor, 1.f, SpinT / SpinUpSeconds) : FMath::FInterpTo(Pitch, Cruise, DeltaTime, 0.6f);
		if (bFinished) SpinT = -1.f;
		Music->SetPitchMultiplier(FMath::Max(PitchFloor, Pitch * DeckSpeed()));   // the engine floors pitch here anyway; keep SongSeconds honest
	}
	if (WinT >= 0.f)
	{
		// Win: a long turntable wind-down, fading out as it drags to the floor.
		constexpr float WindDown = 4.f;
		WinT += DeltaTime;
		const float A = FMath::Clamp(WinT / WindDown, 0.f, 1.f);
		Pitch = FMath::Lerp(1.f, PitchFloor, FMath::Sqrt(A));
		Music->SetPitchMultiplier(FMath::Max(PitchFloor, Pitch * DeckSpeed()));
		Music->SetVolumeMultiplier(1.f - FMath::Clamp((A - 0.5f) * 2.f, 0.f, 1.f));
		if (A >= 1.f) { Music->Stop(); Music = nullptr; WinT = -1.f; }
		return;
	}
	if (FailT < 0.f) return;

	// CDJ brake to the floor, then stutter on the same scrap a few times, then silence.
	constexpr float Brake = 1.4f, Stutter = 0.13f;
	constexpr int32 Repeats = 6;
	FailT += DeltaTime;
	if (FailT < Brake)
	{
		Music->SetPitchMultiplier(FMath::Lerp(1.f, PitchFloor, FailT / Brake));
		return;
	}
	const int32 Step = FMath::FloorToInt((FailT - Brake) / Stutter);
	if (Step >= Repeats)
	{
		Music->Stop();
		Music = nullptr;
		FailT = -1.f;
		return;
	}
	if (Step != FailStep)
	{
		FailStep = Step;
		Music->Play(FailLoopAt);   // jump back to the same scrap each time
		Music->SetVolumeMultiplier(1.f - 0.12f * Step);
	}
}

void UEclipseDanceBattleSubsystem::TickDancers(float Now, float DeltaTime)
{
	HaloFlash = FMath::Max(0.f, HaloFlash - DeltaTime * 1.5f);
	SwitchPump = FMath::Max(0.f, SwitchPump - DeltaTime * 1.2f);
	if (StageLight)
	{
		const EEclipseDanceStyle S = LastBar >= 0 ? Schedule[FMath::Clamp(LastBar, 0, Schedule.Num() - 1)] : EEclipseDanceStyle::Count;
		UPointLightComponent* L = StageLight->PointLightComponent;
		L->SetLightColor(S == EEclipseDanceStyle::Count ? FLinearColor(0.6f, 0.6f, 0.8f) : EclipseDance::StyleColor(S));
		L->SetIntensity(bFinished ? 0.f : (4.f + 50.f * Backbeat) * (bHeatMode ? 2.f : 1.f));
	}
	if (PlayerHalo)
	{
		const bool bDancing = PlayerStyle != EEclipseDanceStyle::Count && !bFinished;
		PlayerHalo->SetLightColor(bDancing ? EclipseDance::StyleColor(PlayerStyle) : FLinearColor::White);
		PlayerHalo->SetIntensity(bDancing ? 3.f + 30.f * HaloFlash : 0.f);
	}

	if (LastBar < 0 || bFinished) return;
	const float B = (SongSeconds(Now) - Track->SegmentStartSeconds()) / Track->BeatSeconds();

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	for (const TPair<TWeakObjectPtr<ACharacter>, FTransform>& It : MeshBase)
	{
		ACharacter* C = It.Key.Get();
		if (!C) continue;
		const bool bIsPlayer = PC && C == PC->GetPawn();
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		StyleMotion(bIsPlayer ? PlayerStyle : OpponentStyle, B, Loc, Rot);
		// Offsets are in actor space, so pre-multiply onto the mesh's own relative rotation.
		C->GetMesh()->SetRelativeLocationAndRotation(It.Value.GetLocation() + Loc, FQuat(Rot) * It.Value.GetRotation());
	}
}
