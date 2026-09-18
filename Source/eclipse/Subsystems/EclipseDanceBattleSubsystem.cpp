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
#include "Quartz/QuartzSubsystem.h"
#include "Quartz/AudioMixerClockHandle.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/PointLight.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	const FName DanceClockName(TEXT("DanceBattle"));

	// ponytail: fixed output-latency guess; make it a settings slider if players report PERFECT feeling late.
	constexpr double AudioLatencySeconds = 0.04;
	constexpr double PerfectWindow = 0.08;
	constexpr int32 IntroBars = 4;
	constexpr int32 HeatLostOnDefeat = 2;
	constexpr float CamTiltDegrees = 6.f;
	constexpr float CamOrbitDegrees = 14.f;
	constexpr float CamBumpSeconds = 0.22f;
	constexpr float BaseFOV = 70.f;

	const TCHAR* GradeNames[] = { TEXT("PERFECT"), TEXT("GOOD"), TEXT("TOO EARLY"), TEXT("TOO SLOW"), TEXT("MISS") };
	const int32 GradePoints[] = { 300, 100, 25, 50, 0 };
	const float GradeValue[] = { 1.f, 0.7f, 0.25f, 0.35f, 0.f };   // feeds Performance

	// Held directions that exactly match a style's combo win; otherwise the single key just pressed.
	EEclipseDanceStyle StyleFromKeys(const APlayerController* PC, EEclipseDanceStyle Current)
	{
		using namespace EclipseDance;
		static const TTuple<uint8, FKey, FKey> Dirs[] = {
			{ Up, EKeys::W, EKeys::Up }, { Left, EKeys::A, EKeys::Left }, { Down, EKeys::S, EKeys::Down }, { Right, EKeys::D, EKeys::Right } };
		uint8 Held = 0, Pressed = 0;
		for (const auto& D : Dirs)
		{
			if (PC->IsInputKeyDown(D.Get<1>()) || PC->IsInputKeyDown(D.Get<2>())) Held |= D.Get<0>();
			if (PC->WasInputKeyJustPressed(D.Get<1>()) || PC->WasInputKeyJustPressed(D.Get<2>())) Pressed |= D.Get<0>();
		}
		if (!Pressed) return Current;
		for (uint8 Mask : { Held, Pressed })
		{
			for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
			{
				if (Info((EEclipseDanceStyle)i).Keys == Mask) return (EEclipseDanceStyle)i;
			}
		}
		return Current;
	}

	// Placeholder moves until the characters are rigged: B is song position in beats.
	void StyleMotion(EEclipseDanceStyle S, float B, FVector& Loc, FRotator& Rot)
	{
		const float Pi = PI;
		switch (S)
		{
		case EEclipseDanceStyle::Hakken:   // stomping double-time bounce, leaning in
			Loc.Z = 10.f * FMath::Abs(FMath::Sin(2.f * Pi * B));
			Rot.Pitch = -6.f + 4.f * FMath::Sin(2.f * Pi * B);
			break;
		case EEclipseDanceStyle::Muzzing:  // side step, one side per beat
			Loc.Y = 12.f * FMath::Sin(Pi * B);
			Loc.Z = 4.f * FMath::Abs(FMath::Sin(2.f * Pi * B));
			Rot.Roll = 5.f * FMath::Sin(Pi * B);
			break;
		case EEclipseDanceStyle::Liquid:   // slow rolling wave over two beats
			Loc.Z = 3.f * FMath::Sin(Pi * B);
			Rot.Roll = 10.f * FMath::Sin(0.5f * Pi * B);
			Rot.Yaw = 15.f * FMath::Sin(0.5f * Pi * B + 1.f);
			break;
		case EEclipseDanceStyle::Gloving:  // head nod on the beat, small twist
			Loc.Z = 3.f * FMath::Abs(FMath::Sin(2.f * Pi * B));
			Rot.Pitch = 8.f * FMath::Sin(2.f * Pi * B);
			Rot.Yaw = 8.f * FMath::Sin(Pi * B);
			break;
		case EEclipseDanceStyle::Tektonik: // hard yaw snaps side to side
			Loc.Z = 6.f * FMath::Abs(FMath::Sin(2.f * Pi * B));
			Rot.Yaw = 25.f * FMath::Clamp(4.f * FMath::Sin(Pi * B), -1.f, 1.f);
			break;
		default:                           // not dancing yet: nodding along
			Loc.Z = 2.f * FMath::Abs(FMath::Sin(Pi * B));
			break;
		}
	}
}

bool UEclipseDanceBattleSubsystem::StartBattle(UEclipseDanceTrackData* InTrack, AEclipseNpcCharacter* InOpponent, bool bTutorial)
{
	if (!InTrack || !InTrack->Sound) return false;
	Stop();

	UWorld* World = GetWorld();
	UQuartzSubsystem* Quartz = World ? World->GetSubsystem<UQuartzSubsystem>() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!Quartz || !PC) return false;

	Track = InTrack;
	Opponent = InOpponent;
	BuildSchedule(InTrack->SegmentBars());
	LastBar = -1;
	LastBeat = 0;
	Score = OpponentScore = 0;
	FMemory::Memzero(Counts);
	Performance = 0.5f;
	bFinished = false;
	PlayerStyle = OpponentStyle = EEclipseDanceStyle::Count;
	StyleChosenAt = 0.0;
	HaloFlash = BeatFlash = 0.f;

	// Proficiency 1-3 per style and the intro barks both live in Ink, keyed by the opponent's knot.
	IntroLines.Reset();
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
	}

	FQuartzClockSettings Settings;
	Settings.TimeSignature.NumBeats = InTrack->BeatsPerBar;
	Clock = Quartz->CreateNewClock(this, DanceClockName, Settings, /*bOverrideSettingsIfClockExists=*/true);
	if (!Clock) { Track = nullptr; return false; }
	UQuartzClockHandle* Handle = Clock;   // Quartz takes the handle by raw-pointer reference

	// Tempo applies immediately; the clock isn't running yet, so there's nothing to quantise it to.
	FQuartzQuantizationBoundary Now{ EQuartzCommandQuantization::None, 1.f, EQuarztQuantizationReference::BarRelative, true };
	Handle->SetBeatsPerMinute(this, Now, FOnQuartzCommandEventBP(), Handle, InTrack->BPM);

	FOnQuartzMetronomeEventBP OnBeatEvent;
	OnBeatEvent.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UEclipseDanceBattleSubsystem, HandleBeat));
	Handle->SubscribeToQuantizationEvent(this, EQuartzCommandQuantization::Beat, OnBeatEvent, Handle);

	Music = UGameplayStatics::CreateSound2D(this, InTrack->Sound, 1.f, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
	if (!Music) { Stop(); return false; }

	// Start the sound on the clock's first bar, seeked to a downbeat, so the clock's bars ARE the track's bars.
	FQuartzQuantizationBoundary FirstBar{ EQuartzCommandQuantization::Bar, 1.f, EQuarztQuantizationReference::BarRelative, true };
	FOnQuartzCommandEventBP OnCommand;
	OnCommand.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UEclipseDanceBattleSubsystem, HandleCommandEvent));
	Music->PlayQuantized(this, Handle, FirstBar, OnCommand, InTrack->SegmentStartSeconds());

	TSubclassOf<UEclipseDanceBattleWidget> WidgetClass = UEclipseDanceBattleWidget::StaticClass();
	if (UClass* BP = LoadClass<UEclipseDanceBattleWidget>(nullptr, TEXT("/Game/Justin/UI/WBP_DanceBattle.WBP_DanceBattle_C")))
	{
		WidgetClass = BP;
	}
	Widget = CreateWidget<UEclipseDanceBattleWidget>(PC, WidgetClass);
	if (Widget)
	{
		Widget->AddToViewport(/*ZOrder=*/300);
		Widget->SetShowNames(bTutorial);
		Widget->ShowStyle(EEclipseDanceStyle::Count);
		Widget->SetRadialSelected(PlayerStyle);
		if (UEclipseWaveformWidget* Wave = Widget->GetWaveform())
		{
			TArray<TPair<float, EEclipseDanceStyle>> Switches;
			for (int32 Bar = 1; Bar < Schedule.Num(); ++Bar)
			{
				if (Schedule[Bar] != Schedule[Bar - 1]) Switches.Emplace(InTrack->SegmentStartSeconds() + Bar * InTrack->BarSeconds(), Schedule[Bar]);
			}
			Wave->SetTrack(InTrack, Switches);
			Wave->SetPlayhead(InTrack->SegmentStartSeconds());
		}
	}
	EnterBattleCamera();

	Handle->StartClock(this, Handle);

	UE_LOG(LogEclipse, Log, TEXT("Dance battle vs %s: %.2f BPM, %d bars from %.2fs%s"),
		*GetNameSafe(InOpponent), InTrack->BPM, InTrack->SegmentBars(), InTrack->SegmentStartSeconds(), bTutorial ? TEXT(" (tutorial)") : TEXT(""));
	return true;
}

void UEclipseDanceBattleSubsystem::BuildSchedule(int32 NumBars)
{
	// Seeded by opponent so a retry dances the same routine.
	FRandomStream Rng(Opponent ? GetTypeHash(Opponent->GetName()) : 7);

	const int32 NumStyles = (int32)EEclipseDanceStyle::Count;
	Schedule.Init(EEclipseDanceStyle::Count, FMath::Min(IntroBars, NumBars));
	EEclipseDanceStyle Current = (EEclipseDanceStyle)Rng.RandRange(0, NumStyles - 1);
	while (Schedule.Num() < NumBars)
	{
		const int32 Run = Rng.FRand() < 0.5f ? 4 : 8;
		for (int32 b = 0; b < Run && Schedule.Num() < NumBars; ++b) Schedule.Add(Current);
		EEclipseDanceStyle Next = Current;
		while (Next == Current) Next = (EEclipseDanceStyle)Rng.RandRange(0, NumStyles - 1);
		Current = Next;
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

	if (AEclipsePlayerCharacter* EP = Cast<AEclipsePlayerCharacter>(Player)) EP->StartFaceTarget(To);
	Opponent->StartFacePlayer(Player);
	PC->SetIgnoreMoveInput(true);   // WSAD picks styles during a battle

	MeshBase.Reset();
	for (ACharacter* C : { Player, static_cast<ACharacter*>(Opponent.Get()) })
	{
		if (C && C->GetMesh()) MeshBase.Add(C, C->GetMesh()->GetRelativeTransform());
	}
}

void UEclipseDanceBattleSubsystem::LeaveBattleCamera()
{
	for (const TPair<TWeakObjectPtr<ACharacter>, FTransform>& It : MeshBase)
	{
		if (ACharacter* C = It.Key.Get()) C->GetMesh()->SetRelativeTransform(It.Value);
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
	}
	if (Opponent) Opponent->StopFacePlayer();
	if (BattleCamera) BattleCamera->SetLifeSpan(1.f);   // outlive the blend back
	BattleCamera = nullptr;
	if (StageLight) StageLight->Destroy();
	StageLight = nullptr;
	if (PlayerHalo) PlayerHalo->DestroyComponent();
	PlayerHalo = nullptr;
}

void UEclipseDanceBattleSubsystem::Stop()
{
	if (!Track) return;
	if (Music)
	{
		Music->FadeOut(0.4f, 0.f);
		Music = nullptr;
	}
	if (Clock)
	{
		UQuartzClockHandle* Handle = Clock;
		Handle->StopClock(this, /*CancelPendingEvents=*/true, Handle);
		Clock = nullptr;
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

void UEclipseDanceBattleSubsystem::HandleBeat(FName ClockName, EQuartzCommandQuantization QuantizationType, int32 NumBars, int32 Beat, float BeatFraction)
{
	if (!Track || bFinished) return;

	// Quartz counts bars from 1; the battle counts from 0.
	const int32 Bar = FMath::Max(0, NumBars - 1);
	if (Bar >= Schedule.Num())
	{
		Finish();
		return;
	}

	const double Now = FPlatformTime::Seconds();
	LastBeatTime = Now;
	LastBar = Bar;
	LastBeat = Beat;
	BeatFlash = 1.f;
	OnBeat.Broadcast(Bar, Beat);

	// Intro barks, spread evenly across the intro bars and held until the next one.
	if (Beat == 1 && Bar < IntroBars && Opponent && IntroLines.Num() > 0)
	{
		const int32 Stride = FMath::Max(1, IntroBars / IntroLines.Num());
		if (Bar % Stride == 0 && IntroLines.IsValidIndex(Bar / Stride))
		{
			Opponent->Yap(IntroLines[Bar / Stride], Stride * Track->BarSeconds() - 0.2f);
		}
	}

	const EEclipseDanceStyle Style = Schedule[Bar];
	const bool bSwitchBar = Bar > 0 && Style != Schedule[Bar - 1];
	if (bSwitchBar && Beat == 1)
	{
		SwitchTime = Now + AudioLatencySeconds;
		RollOpponentSwitch(Bar);
		if (Widget) Widget->ShowStyle(Style);
		CamRollTarget = CamRollTarget > 0.f ? -CamTiltDegrees : CamTiltDegrees;
		CamOrbitTarget = CamOrbitTarget > 0.f ? -CamOrbitDegrees : CamOrbitDegrees;
		CamBump = 0.f;
	}
	// Grade a beat after the switch so a slightly late press still counts as TOO SLOW rather than a miss.
	if (bSwitchBar && Beat == 2) GradeSwitch(Bar);

	if (!Widget) return;
	const FLinearColor Pulse = Style == EEclipseDanceStyle::Count ? FLinearColor(1.f, 1.f, 1.f, 0.5f) : EclipseDance::StyleColor(Style);
	Widget->Pulse(Pulse, Track->BeatSeconds());
	// The bar before a switch counts it in on its own beats: 4, 3, 2, 1.
	const bool bSwitchNext = Schedule.IsValidIndex(Bar + 1) && Schedule[Bar + 1] != Style;
	Widget->ShowCountdown(bSwitchNext ? Track->BeatsPerBar + 1 - Beat : 0,
		bSwitchNext ? Schedule[Bar + 1] : Style, Track->BeatSeconds());
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

	EGrade G;
	if (PlayerStyle != Schedule[Bar])  G = EGrade::Miss;
	else if (Dt < -2.0 * Beat)         G = EGrade::TooEarly;
	else if (Dt < -PerfectWindow)      G = EGrade::Good;
	else if (Dt <= PerfectWindow)      G = EGrade::Perfect;
	else                               G = EGrade::TooSlow;

	++Counts[(int32)G];
	Score += GradePoints[(int32)G];
	Performance = FMath::Lerp(Performance, GradeValue[(int32)G], 0.4f);

	if (bOpponentHit)
	{
		const int32 Level = OpponentLevel[(int32)Schedule[Bar]];
		OpponentScore += FMath::FRand() < 0.12f * Level ? GradePoints[(int32)EGrade::Perfect] : GradePoints[(int32)EGrade::Good];
	}

	const FLinearColor Color = G == EGrade::Perfect ? EclipseDance::StyleColor(Schedule[Bar])
	                         : G == EGrade::Good    ? FLinearColor::White
	                         : G == EGrade::Miss    ? EclipseUI::DialogueRed
	                         :                        FLinearColor(0.6f, 0.6f, 0.6f);
	if (Widget) Widget->ShowGrade(FText::FromString(GradeNames[(int32)G]), Color);
	UE_LOG(LogEclipse, Log, TEXT("Dance switch bar %d: %s (%+.0f ms) score %d vs %d"), Bar, GradeNames[(int32)G], Dt * 1000.0, Score, OpponentScore);
}

void UEclipseDanceBattleSubsystem::Finish()
{
	bFinished = true;
	if (Clock)
	{
		UQuartzClockHandle* Handle = Clock;
		Handle->StopClock(this, /*CancelPendingEvents=*/true, Handle);
		Clock = nullptr;
	}
	if (Music) Music->FadeOut(6.f, 0.f);

	const bool bWon = Score >= OpponentScore;
	if (!bWon)
	{
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
		{
			GS->ChangeMeter(TEXT("heat"), -HeatLostOnDefeat);
		}
	}

	float Hold = 3.f;
	if (Widget)
	{
		Widget->ShowCountdown(0, EEclipseDanceStyle::Count, 0.f);
		TArray<TPair<FString, int32>> Rows;
		for (int32 i = 0; i < (int32)EGrade::Count; ++i) Rows.Emplace(GradeNames[i], Counts[i]);
		Rows.Emplace(TEXT("SCORE"), Score);
		Rows.Emplace(Opponent ? Opponent->GetDisplayName().ToString().ToUpper() : TEXT("RIVAL"), OpponentScore);
		const FString Verdict = bWon ? TEXT("YOU WIN") : FString::Printf(TEXT("YOU LOSE   HEAT -%d"), HeatLostOnDefeat);
		Hold += Widget->ShowResults(Rows, Verdict, bWon ? FLinearColor::White : EclipseUI::DialogueRed);
	}
	UE_LOG(LogEclipse, Log, TEXT("Dance battle over: %s, %d vs %d"), bWon ? TEXT("won") : TEXT("lost"), Score, OpponentScore);

	FTimerHandle Timer;
	GetWorld()->GetTimerManager().SetTimer(Timer, FTimerDelegate::CreateWeakLambda(this, [this] { Stop(); }), Hold, false);
}

void UEclipseDanceBattleSubsystem::HandleCommandEvent(EQuartzCommandDelegateSubType EventType, FName Name)
{
	if (EventType == EQuartzCommandDelegateSubType::CommandOnStarted)
	{
		UE_LOG(LogEclipse, Log, TEXT("Dance battle: track started on the bar"));
	}
}

float UEclipseDanceBattleSubsystem::SongSeconds(double Now) const
{
	if (!Track || LastBar < 0) return Track ? Track->SegmentStartSeconds() : 0.f;
	const double Frac = FMath::Min(1.0, (Now - LastBeatTime) / Track->BeatSeconds());
	const double Beats = LastBar * Track->BeatsPerBar + (LastBeat - 1) + Frac;
	return Track->SegmentStartSeconds() + Beats * Track->BeatSeconds() - AudioLatencySeconds;
}

void UEclipseDanceBattleSubsystem::Tick(float DeltaTime)
{
	if (!Track) return;
	const double Now = FPlatformTime::Seconds();

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (PC && !bFinished && LastBar >= 0)
	{
		const EEclipseDanceStyle Picked = StyleFromKeys(PC, PlayerStyle);
		if (Picked != PlayerStyle)
		{
			PlayerStyle = Picked;
			StyleChosenAt = Now;
			HaloFlash = 1.f;
			if (Widget) Widget->SetRadialSelected(Picked);
		}
	}

	if (Widget)
	{
		if (UEclipseWaveformWidget* Wave = Widget->GetWaveform()) Wave->SetPlayhead(SongSeconds(Now));
	}
	TickCamera(DeltaTime);
	TickDancers(Now, DeltaTime);
}

void UEclipseDanceBattleSubsystem::TickCamera(float DeltaTime)
{
	if (!BattleCamera) return;
	CamRoll = FMath::FInterpTo(CamRoll, CamRollTarget, DeltaTime, 8.f);
	CamOrbit = FMath::FInterpTo(CamOrbit, CamOrbitTarget, DeltaTime, 5.f);
	CamBump = FMath::Min(1.f, CamBump + DeltaTime / CamBumpSeconds);

	// 0 → 1 → 0: a quick punch. Dancing well punches in hard; dancing badly pulls out instead.
	const float Bump = FMath::Sin(PI * CamBump);
	const float Punch = FMath::Lerp(-0.8f, 1.8f, Performance) * Bump;
	const FVector Loc = CamMid + CamOffset.RotateAngleAxis(CamOrbit, FVector::UpVector);
	FRotator Rot = (CamMid - Loc).Rotation();
	Rot.Roll = CamRoll;
	BattleCamera->SetActorLocationAndRotation(Loc + Rot.Vector() * 30.f * Punch, Rot);
	BattleCamera->GetCameraComponent()->SetFieldOfView(BaseFOV - 7.f * Punch);
}

void UEclipseDanceBattleSubsystem::TickDancers(float Now, float DeltaTime)
{
	HaloFlash = FMath::Max(0.f, HaloFlash - DeltaTime * 1.5f);
	BeatFlash = FMath::Max(0.f, BeatFlash - DeltaTime * 4.f);
	if (StageLight)
	{
		const EEclipseDanceStyle S = LastBar >= 0 ? Schedule[FMath::Clamp(LastBar, 0, Schedule.Num() - 1)] : EEclipseDanceStyle::Count;
		UPointLightComponent* L = StageLight->PointLightComponent;
		L->SetLightColor(S == EEclipseDanceStyle::Count ? FLinearColor(0.6f, 0.6f, 0.8f) : EclipseDance::StyleColor(S));
		L->SetIntensity(bFinished ? 0.f : 6.f + 40.f * BeatFlash * BeatFlash);
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
