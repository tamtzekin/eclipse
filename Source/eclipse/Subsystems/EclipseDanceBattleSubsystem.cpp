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
#include "Blueprint/WidgetLayoutLibrary.h"
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
	constexpr float NoteWindow = 0.25f;        // seconds either side of his attack to answer it
	constexpr float NoteDamage = 9.f;          // STANCE off a matched attack, before the style multiplier
	constexpr float StyleMatchBonus = 2.f;     // dancing his exact style doubles what lands
	constexpr float CamTiltDegrees = 6.f;
	constexpr float CamOrbitDegrees = 14.f;
	constexpr float CamBumpSeconds = 0.22f;
	constexpr float BaseFOV = 70.f;
	constexpr float SpinUpSeconds = 3.5f;   // vinyl start: pitch ramps from the floor to 1 over this long
	constexpr float PitchFloor = 0.4f;      // the engine's default global minimum pitch
	constexpr float StickDeadzone = 0.6f;
	constexpr int32 Lookahead = 5;            // bars of schedule kept ahead of the song (leads + countdown need 3)
	constexpr float HoldFacingToPass = 4.f;   // seconds facing him without a break to pass the strafe lesson
	constexpr int32 StrafeMinBars = 8;        // he moves for at least this long, so the lesson is visible
	constexpr int32 MovesetBars = 4;           // he runs his pattern once every four bars
	constexpr int32 LessonPatterns = 2;        // moves he teaches before the scored battle
	constexpr float TelegraphLessonBeats = 8.f, TelegraphBattleBeats = 4.f;   // two bars to tell you in the lesson, one in the battle
	constexpr float BattleSeconds = 80.f;     // the scored part, after the intro and the demo
	constexpr float StrafeDegPerSec = 20.f;   // a slow sidestep, not a run
	constexpr float StrafeLimitDeg = 50.f;    // either side of where the battle started: a 180-degree arc
	constexpr float FacingWindowDeg = 30.f;   // off by more than this when a switch lands and it's a MISS
	constexpr float PlayerStanceMax = 100.f;   // yours; full means staggered
	constexpr float StaggerSeconds = 2.8f;     // how long you're caught flat-footed
	constexpr float StanceMax = 260.f;         // the opponent's STANCE; break it and the battle's won early
	constexpr int32 HeatStreak = 8;           // clean answers in a row that light HEAT mode
	constexpr float HeatModeBars = 8.f;       // how long it lasts, in bars
	constexpr float HeatModeSpeedUp = 0.12f;  // extra tempo while it's on
	constexpr float HeatDamage = 1.5f;        // everything you land hits this much harder
	constexpr float StanceRecover = 10.f;     // he steadies this much when you MISS

	const TCHAR* GradeNames[] = { TEXT("PERFECT"), TEXT("GOOD"), TEXT("TOO EARLY"), TEXT("TOO SLOW"), TEXT("MISS"), TEXT("HOT") };
	const int32 GradePoints[] = { 300, 100, 25, 50, 0, 500 };
	const float GradeValue[] = { 1.f, 0.7f, 0.25f, 0.35f, 0.f, 1.f };   // feeds Performance
	const float GradeZoom[] = { -14.f, -9.f, 10.f, 0.f, 16.f, -16.f };  // FOV change per grade: good dancing draws the camera right in
	const int32 GradeXP[] = { 25, 10, 0, 0, 0, 30 };                    // style XP for landing a switch
	const float GradeDamage[] = { 20.f, 12.f, 3.f, 5.f, 0.f, 30.f };    // STANCE damage before the style-level multiplier

	// Colour per grade: PERFECT white, GOOD gold, the rest cooler, MISS red.
	FLinearColor GradeColor(int32 G)
	{
		const FLinearColor Colors[] = { FLinearColor::White, EclipseDance::Gold, FLinearColor(FColor(0xCD, 0x7F, 0x32)),
			FLinearColor(FColor(0xC8, 0xD0, 0xDA)), EclipseUI::DialogueRed, FLinearColor(FColor(0xFF, 0x5A, 0x1F)) };
		return Colors[FMath::Clamp(G, 0, (int32)UE_ARRAY_COUNT(Colors) - 1)];
	}

	// H or the left shoulder cycles your style; the right stick still points straight at a wheel slot.
	EEclipseDanceStyle StyleFromInput(const APlayerController* PC, EEclipseDanceStyle Current, bool& bStickLatched, int32 Unlocked)
	{
		using namespace EclipseDance;
		const auto Open = [Unlocked](int32 i) { return ((Unlocked >> i) & 1) != 0; };
		if (PC->WasInputKeyJustPressed(EKeys::H) || PC->WasInputKeyJustPressed(EKeys::Gamepad_LeftShoulder))
		{
			for (int32 Step = 1; Step <= (int32)EEclipseDanceStyle::Count; ++Step)
			{
				const int32 Next = ((int32)Current + Step) % (int32)EEclipseDanceStyle::Count;
				if (Open(Next)) return (EEclipseDanceStyle)Next;
			}
		}

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
		case EEclipseDanceStyle::Jumpstyle:  // big jumps off both feet
			Loc.Z = 20.f * Bounce;
			break;
		case EEclipseDanceStyle::Shuffle:    // quick little steps, barely leaving the floor
			Loc.Y = 7.f * FMath::Sin(2.f * Pi * B);
			Loc.Z = 4.f * Bounce;
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
	bTutorial = true;
	bBeatenBefore = false;
	if (UEclipseDialogueSubsystem* Beaten = World->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
	{
		bBeatenBefore = InOpponent && Beaten->GetInkInt(InOpponent->DialogueId.ToString() + TEXT("_dance_won"), 0) == 1;
	}
	if (UEclipseGameStateSubsystem* GS = World->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>())
	{
		Unlocked = GS->UnlockedDanceStyles;
		bTutorial = (InTrack->bTutorialTrack || !GS->bDanceTutorialDone) && !bBeatenBefore;
	}
	BuildSchedule();
	LastBar = -1;
	Score = OpponentScore = 0;
	Streak = 0;
	bHeatMode = false;
	FMemory::Memzero(Counts);
	Performance = 0.5f;
	bFinished = false;
	PlayerStyle = OpponentStyle = EEclipseDanceStyle::Count;
	HaloFlash = BeatFlash = 0.f;
	BeatIndex = -1;
	SongClock = InTrack->SegmentStartSeconds();
	bWarnedPlayback = false;
	Pitch = PitchFloor;
	WinT = -1.f;
	Wonk = CamTime = 0.f;
	CamFOV = CamFOVTarget = BaseFOV;
	CamTimeDrift = 0.f;
	OpponentStance = StanceMax;
	bStickLatched = false;
	SpinT = -1.f;

	// Proficiency 1-3 per style and the intro barks both live in Ink, keyed by the opponent's knot.
	IntroLines.Reset();
	StrafeLines.Reset();
	BothLine.Reset();
	for (int32& L : OpponentLevel) L = 1;
	if (UEclipseDialogueSubsystem* DS = World->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>(); DS && Opponent)
	{
		const FString Id = Opponent->DialogueId.ToString();
		for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
		{
			const FString Var = FString::Printf(TEXT("%s_%s"), *Id, *FString(EclipseDance::Info((EEclipseDanceStyle)i).Name).ToLower());
			OpponentLevel[i] = FMath::Clamp(DS->GetInkInt(Var, 1), 0, 3);
		}
		WrongHeavyLines = DS->ReadKnotLines(Id + TEXT("_wrong_heavy"));
		WrongLightLines = DS->ReadKnotLines(Id + TEXT("_wrong_light"));
		Movesets = DS->ReadKnotLines(Id + TEXT("_moveset"));
		if (Movesets.Num() == 0) Movesets = { TEXT("L L L L L H L L") };   // the tutorial pattern, all on the beat
		MovesetIndex = 0;
		Notes.Reset();
		NotesBar = -1;
		PatternHits = PatternNotes = LearnPasses = 0;
		MovesetStart = MAX_int32;
		IntroLines = DS->ReadKnotLines(Id + (bBeatenBefore ? TEXT("_battle_intro_again") : TEXT("_battle_intro")));
		if (IntroLines.Num() == 0) IntroLines = DS->ReadKnotLines(Id + TEXT("_battle_intro"));
		StrafeLines = DS->ReadKnotLines(Id + TEXT("_battle_strafe"));
		const TArray<FString> Both = DS->ReadKnotLines(Id + TEXT("_battle_both"));
		BothLine = Both.Num() ? Both[0] : FString();
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
	}
	EnterBattleCamera();
	if (Widget && InOpponent) Widget->ShowExpert(InOpponent->GetDisplayName().ToString(), InOpponent->ExpertStyle);
	SpawnWaveWall();
	SetGameUiHidden(true);

	UE_LOG(LogEclipse, Log, TEXT("Dance battle vs %s: %.2f BPM, %d bars from %.2fs"),
		*GetNameSafe(InOpponent), InTrack->BPM, Schedule.Num(), InTrack->SegmentStartSeconds());
	return true;
}

void UEclipseDanceBattleSubsystem::BuildSchedule()
{
	// Seeded by track so a retry dances the same routine.
	Rng.Initialize(GetTypeHash(Track->GetName()));

	// The battle draws on what the player knows plus whatever this track introduces.
	Pool.Reset();
	for (int32 i = 0; i < (int32)EEclipseDanceStyle::Count; ++i)
	{
		if ((Unlocked >> i) & 1) Pool.Add((EEclipseDanceStyle)i);
	}
	if (Pool.Num() < 2) Pool = { EEclipseDanceStyle::Muzzing, EEclipseDanceStyle::Tektonik };

	// The track's own length caps the whole session.
	const float TrackSeconds = Track->Envelope.Num() > 0 ? Track->Envelope.Num() / Track->EnvelopeRate : Track->Sound->Duration;
	MaxBars = FMath::Max(IntroBars, FMath::FloorToInt((TrackSeconds - Track->SegmentStartSeconds()) / Track->BarSeconds()) - 1);

	Schedule.Init(EEclipseDanceStyle::Count, IntroBars);
	Phase = EPhase::Intro;
	StrafeStart = -1;
	DemoStart = DemoEnd = BattleStart = MAX_int32;
	FacedSeconds = 0.f;
	PatternHits = PatternNotes = LearnPasses = 0;
	AppendSchedule();
}

void UEclipseDanceBattleSubsystem::AppendSchedule()
{
	const int32 Before = Schedule.Num();
	const auto Add = [this](EEclipseDanceStyle S, int32 Bars) { for (int32 b = 0; b < Bars && Schedule.Num() < MaxBars; ++b) Schedule.Add(S); };
	const auto PickOther = [this](EEclipseDanceStyle Not)
	{
		EEclipseDanceStyle S = Not;
		while (S == Not) S = Pool[Rng.RandRange(0, Pool.Num() - 1)];
		return S;
	};
	const auto StartBattlePhase = [&]()
	{
		Phase = EPhase::Battle;
		BattleStart = Schedule.Num();
		MovesetStart = FMath::Min(MovesetStart, BattleStart);
		if (UEclipseGameStateSubsystem* GS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseGameStateSubsystem>()) GS->bDanceTutorialDone = true;
		const int32 End = FMath::Min(MaxBars, BattleStart + FMath::RoundToInt(BattleSeconds / Track->BarSeconds()));
		// One style throughout: the battle is his moves and your footing.
		const EEclipseDanceStyle Current = Pool.Contains(EEclipseDanceStyle::Muzzing) ? EEclipseDanceStyle::Muzzing : Pool[0];
		for (int32 Bar = BattleStart; Bar < End; ++Bar) Schedule.Add(Current);
	};
	const auto StartLearnPhase = [&]()
	{
		// He loops a pattern and you copy it; the schedule just holds his style while that happens.
		while (Schedule.Num() % MovesetBars != 0) Add(EEclipseDanceStyle::Count, 1);   // land the switch on a loop boundary
		DemoStart = Schedule.Num();
		if (!bTutorial) { DemoEnd = Schedule.Num(); StartBattlePhase(); return; }
		Phase = EPhase::Learn;
		MovesetStart = Schedule.Num();
		Add(Pool.Contains(EEclipseDanceStyle::Muzzing) ? EEclipseDanceStyle::Muzzing : Pool[0], 2);
		DemoEnd = Schedule.Num();
	};

	switch (Phase)
	{
	case EPhase::Intro:
		if (bTutorial) { Phase = EPhase::Strafe; StrafeStart = Schedule.Num(); Add(EEclipseDanceStyle::Count, 1); }
		else StartLearnPhase();
		break;
	case EPhase::Strafe:
		// A bar at a time until they've held facing him for long enough, and never shorter than the lesson needs to read.
		if (FacedSeconds >= HoldFacingToPass && Schedule.Num() - StrafeStart >= StrafeMinBars) StartLearnPhase();
		else Add(EEclipseDanceStyle::Count, 1);
		break;
	case EPhase::Learn:
		// Copy two of his patterns cleanly and the real thing starts.
		if (LearnPasses >= LessonPatterns) StartBattlePhase();
		else { Add(Schedule.Last(), 2); DemoEnd = Schedule.Num(); }
		break;
	default:
		break;
	}

	if (Schedule.Num() != Before && Wave)
	{
		TArray<TPair<float, EEclipseDanceStyle>> Switches;
		for (int32 Bar = 1; Bar < Schedule.Num(); ++Bar)
		{
			if (Schedule[Bar] != Schedule[Bar - 1] && Schedule[Bar] != EEclipseDanceStyle::Count) Switches.Emplace(Track->SegmentStartSeconds() + Bar * Track->BarSeconds(), Schedule[Bar]);
		}
		Wave->SetTrack(Track, Switches);
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
		if (Schedule[Bar] != Schedule[Bar - 1] && Schedule[Bar] != EEclipseDanceStyle::Count) Switches.Emplace(Track->SegmentStartSeconds() + Bar * Track->BarSeconds(), Schedule[Bar]);
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
	// HEAT, THIRST, the clock, prompts — every bit of game UI steps aside, leaving only the battle's own.
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UUserWidget::StaticClass(), /*TopLevelOnly=*/true);
	for (UUserWidget* W : Found)
	{
		if (!W->IsVisible() || W == Widget) continue;
		W->SetVisibility(ESlateVisibility::Collapsed);
		HiddenUi.Add(W);
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
	StrafeRadius = FMath::Clamp(FVector::Dist2D(From, To) * 0.5f, 70.f, 100.f);   // a tight circle: you shuffle round each other rather than orbit
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
	if (Widget && Opponent)
	{
		// His bar rides above his head rather than sitting in a corner.
		FVector2D Screen;
		APlayerController* HeadPC = GetWorld()->GetFirstPlayerController();
		const bool bOn = HeadPC && !bFinished && UGameplayStatics::ProjectWorldToScreen(HeadPC, Opponent->GetActorLocation() + FVector(0.f, 0.f, 130.f), Screen);
		Widget->SetStanceScreenPos(Screen / FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(GetWorld())), bOn);
	}
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
	// Beaten, he talks: <npc>_defeated picks up where the battle left off.
	const FName DefeatKnot = bPendingDefeatTalk && Opponent ? FName(*(Opponent->DialogueId.ToString() + TEXT("_defeated"))) : NAME_None;
	bPendingDefeatTalk = false;
	Track = nullptr;
	Opponent = nullptr;
	if (!DefeatKnot.IsNone())
	{
		if (UEclipseDialogueSubsystem* DS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>()) DS->OpenKnot(DefeatKnot);
	}
}

void UEclipseDanceBattleSubsystem::HandleBeat(int32 Bar, int32 Beat)
{
	if (!Track || bFinished) return;

	// Lessons end when they're mastered, so the schedule is written a few bars ahead as the song goes.
	const int32 Ahead = Phase == EPhase::Battle ? Lookahead : 3;   // open-ended lessons stay close so passing moves on quickly
	for (int32 Was = -1; Beat == 1 && Phase != EPhase::Battle && Schedule.Num() - Bar < Ahead && Schedule.Num() != Was;)
	{
		Was = Schedule.Num();
		AppendSchedule();
	}
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
	if (Beat == 1 && Bar == IntroBars && Widget) Widget->ShowExpert(FString(), EEclipseDanceStyle::Count);   // shown once on engage, then gone
	if (Beat == 1 && Bar < IntroBars && Opponent && IntroLines.Num() > 0)
	{
		const int32 Stride = FMath::Max(1, IntroBars / IntroLines.Num());
		if (Bar % Stride == 0 && IntroLines.IsValidIndex(Bar / Stride))
		{
			Opponent->Yap(IntroLines[Bar / Stride], Stride * Track->BarSeconds() - 0.2f, FLinearColor::White, BeatSec);
		}
	}

	const EEclipseDanceStyle Style = Schedule[Bar];

	// Strafe lesson: he talks you through keeping up while he moves, a line every two bars.
	if (Beat == 1 && Opponent && StrafeStart >= 0 && Bar >= StrafeStart && Bar < DemoStart && (Bar - StrafeStart) % 2 == 0)
	{
		const FString Line = Bar == StrafeStart ? TEXT("Keep up with me.")
			: StrafeLines.Num() ? StrafeLines[((Bar - StrafeStart) / 2) % StrafeLines.Num()] : FString();
		if (!Line.IsEmpty()) Opponent->Yap(Line, BarHold * 2.f, FLinearColor::White, BeatSec);
	}
	if (Beat == 1 && Style != EEclipseDanceStyle::Count && Style != OpponentStyle)
	{
		OpponentStyle = Style;
		if (Widget) Widget->ShowStyle(Style);
	}
	// His next pattern, called out a bar before it lands. Only once the lessons are over.
	// Queued well before he dances it: the tell needs two bars in the lesson, one in the battle.
	if (Beat == 1)
	{
		const int32 Ahead = Phase == EPhase::Learn ? 2 : 1;
		const int32 His = Bar + Ahead;
		if (His >= MovesetStart && His > NotesBar)
		{
			if (Phase == EPhase::Learn) { if (His % MovesetBars == 0) StartMoveset(His); }
			else if (His % 2 == 0) StartMoveset(His);
		}
	}
	}

// The lesson is call and response: he dances the pattern on his bar, you answer it on the next.
// In the battle he just keeps throwing them and you answer live.
void UEclipseDanceBattleSubsystem::StartMoveset(int32 Bar)
{
	if (!Track || Movesets.Num() == 0) return;
	// Answered the last one in full? He moves on. Fluffed it? He runs it again.
	if (PatternNotes > 0)
	{
		const bool bClean = PatternHits >= PatternNotes;
		if (bClean && PatternPerfects >= PatternNotes)
		{
			// Move-for-move, every note perfect: the best you can do with one of his patterns.
			Score += GradePoints[(int32)EGrade::Perfect] * PatternNotes;
			if (Phase == EPhase::Battle) OpponentStance = FMath::Clamp(OpponentStance - NoteDamage * PatternNotes, 0.f, StanceMax);
			if (Widget) Widget->FlashChain(TEXT("MOVE MATCHED"), EclipseDance::StyleColor(OpponentStyle));
		}
		const bool bLearned = bClean;
		if (bLearned) { ++MovesetIndex; ++LearnPasses; }
		if (Opponent && Phase == EPhase::Learn)
		{
			Opponent->Yap(bLearned ? TEXT("Got it. Next one.") : bClean ? TEXT("Yeah. Again.") : TEXT("No. Watch me."),
				Track->BarSeconds() / Pitch, FLinearColor::White, Track->BeatSeconds() / Pitch);
		}
	}
	// The lesson walks his moves in order; once it's over he mixes them.
	const int32 Pick = Phase == EPhase::Learn ? MovesetIndex % Movesets.Num() : Rng.RandRange(0, Movesets.Num() - 1);
	const TArray<EclipseDance::FNote> Parsed = EclipseDance::ParseMoveset(Movesets[Pick]);
	if (Parsed.Num() == 0) return;

	const bool bLesson = Phase == EPhase::Learn;
	const float BarSec = Track->BarSeconds(), BeatSec = Track->BeatSeconds();
	const float His = Track->SegmentStartSeconds() + Bar * BarSec;
	Notes.Reset();
	NotesBar = Bar;
	for (const EclipseDance::FNote& N : Parsed)
	{
		// Answer him move-for-move as he dances it — that's the perfect read.
		FPendingNote H;
		H.Time = His + N.Beat * BeatSec;
		H.bHeavy = N.bHeavy;
		H.bHis = true;
		H.bAnswer = true;
		H.bWithHim = true;
		Notes.Add(H);
		if (!bLesson) continue;
		// Or answer it back on the next bar: the call and response, worth good rather than perfect.
		FPendingNote Reply = H;
		Reply.Time += BarSec;
		Reply.bHis = false;
		Reply.bWithHim = false;
		Notes.Add(Reply);
	}
	PatternHits = 0;
	PatternPerfects = 0;
	PatternNotes = Parsed.Num();   // one pass through his move, whichever window you use

	TelegraphDir = Parsed[0].bHeavy ? -1.f : 1.f;   // rises for a light lead, sinks for a heavy one
	TelegraphTo = His + Parsed[0].Beat * BeatSec;
	TelegraphFrom = TelegraphTo - (bLesson ? TelegraphLessonBeats : TelegraphBattleBeats) * BeatSec;
	const FString Text = EclipseDance::MovesetText(Parsed);
	if (Opponent) Opponent->Yap(Text, BarSec / Pitch, EclipseDance::StyleColor(OpponentStyle), BeatSec / Pitch);
}

// Wrong answers cost you footing; at the top you're staggered and can't act for a moment.
void UEclipseDanceBattleSubsystem::AddPlayerStance(float Amount)
{
	if (StaggeredUntil > 0.f) return;
	PlayerStance = FMath::Clamp(PlayerStance + Amount, 0.f, PlayerStanceMax);
	CheckBalance();
}

// The bar only grows when you're being knocked off: drifting off him, or answering wrong.
void UEclipseDanceBattleSubsystem::CheckBalance()
{
	const float Balance = FMath::Clamp(PlayerStance / PlayerStanceMax, 0.f, 1.f);
	if (StaggeredUntil < 0.f && Balance >= 1.f)
	{
		StaggeredUntil = SongSeconds(FPlatformTime::Seconds()) + StaggerSeconds;
		Wonk = FMath::Min(Wonk + 1.5f, 2.f);
		if (Widget) Widget->ShowGrade(FText::FromString(TEXT("BALANCE BROKEN")), EclipseUI::DialogueRed, Track ? Track->BeatSeconds() / Pitch : 0.f);
	}
	if (Widget) Widget->SetPlayerStance(Balance, StaggeredUntil > 0.f);
}

// Notes you never answered: he lands them and keeps his footing.
void UEclipseDanceBattleSubsystem::TickNotes(float Song)
{
	for (FPendingNote& N : Notes)
	{
		// The camera braces a moment early; his body moves exactly on the step.
		if (N.bHis && !N.bCued && Song >= N.Time - 0.2f)
		{
			N.bCued = true;
			if (N.bHeavy) { CamFOVTarget = FMath::Clamp(CamFOVTarget + 7.f, 34.f, 100.f); CamBump = 0.f; }
			else BeatFlash = 1.f;
		}
		if (N.bHis && !N.bStruck && Song >= N.Time)
		{
			N.bStruck = true;
			(N.bHeavy ? OpponentTilt.Y : OpponentTilt.X) = 1.f;
		}
		if (!N.bAnswer || N.bDone || Song <= N.Time + NoteWindow) continue;
		N.bDone = true;
		AddPlayerStance(9.f);   // letting one through costs you footing
		Streak = 0;
		if (Phase == EPhase::Battle) OpponentStance = FMath::Clamp(OpponentStance + StanceRecover * 0.5f, 0.f, StanceMax);
		++Counts[(int32)EGrade::Miss];
		if (Widget)
		{
			Widget->ShowGrade(FText::FromString(GradeNames[(int32)EGrade::Miss]), GradeColor((int32)EGrade::Miss));
			Widget->SetStance(OpponentStance / StanceMax, false);
		}
	}
}

// Your answer to his attack: same weight, on time. His exact style doubles the damage.
void UEclipseDanceBattleSubsystem::Attack(bool bHeavy)
{
	const float Song = SongSeconds(FPlatformTime::Seconds());
	if (StaggeredUntil > 0.f) return;   // flat-footed: nothing lands
	HaloFlash = 1.f;
	(bHeavy ? PlayerTilt.Y : PlayerTilt.X) = 1.f;   // the camera stays out of your own attacks

	FPendingNote* Best = nullptr;
	float BestDt = NoteWindow;
	for (FPendingNote& N : Notes)
	{
		const float Dt = FMath::Abs(Song - N.Time);
		if (!N.bAnswer || N.bDone || Dt > BestDt) continue;
		Best = &N;
		BestDt = Dt;
	}
	if (!Best)
	{
		AddPlayerStance(6.f);
		Streak = 0;
		if (Widget) Widget->ShowGrade(FText::FromString(GradeNames[(int32)EGrade::Miss]), GradeColor((int32)EGrade::Miss), Track->BeatSeconds() / Pitch);
		return;
	}
	Best->bDone = true;
	Best->bHit = Best->bHeavy == bHeavy;
	if (Best->bHeavy != bHeavy)
	{
		++Counts[(int32)EGrade::Miss];
		AddPlayerStance(14.f);   // answering the wrong weight costs more than missing
		Streak = 0;
		Wonk = FMath::Min(Wonk + 0.6f, 2.f);
		if (Widget) Widget->ShowGrade(FText::FromString(GradeNames[(int32)EGrade::Miss]), GradeColor((int32)EGrade::Miss), Track->BeatSeconds() / Pitch);
		// He names the limb you should have used.
		const TArray<FString>& Lines = bHeavy ? WrongHeavyLines : WrongLightLines;
		if (Opponent && Lines.Num()) Opponent->Yap(Lines[FMath::RandRange(0, Lines.Num() - 1)], Track->BarSeconds() / Pitch, EclipseUI::DialogueRed, Track->BeatSeconds() / Pitch);
		return;
	}

	++PatternHits;
	++Streak;
	if (!bHeatMode && Streak >= HeatStreak)
	{
		bHeatMode = true;
		HeatModeEnds = Song + HeatModeBars * Track->BarSeconds();
		if (Wave) Wave->SetHot(true);
		if (Widget)
		{
			Widget->ShowGrade(FText::FromString(TEXT("HEAT")), EclipseDance::Gold, Track->BeatSeconds() / Pitch);
			Widget->Pulse(EclipseDance::Gold, 0.4f, 2.f);
		}
	}
	// With him is perfect; answering back on the next bar is good.
	const EGrade G = Best->bWithHim ? EGrade::Perfect : EGrade::Good;
	if (G == EGrade::Perfect) ++PatternPerfects;
	const bool bStyleMatch = PlayerStyle != EEclipseDanceStyle::Count && PlayerStyle == OpponentStyle;
	const float Damage = NoteDamage * (G == EGrade::Perfect ? 1.4f : 1.f) * (bStyleMatch ? StyleMatchBonus : 1.f) * (bHeatMode ? HeatDamage : 1.f);
	OpponentStance = FMath::Clamp(OpponentStance - Damage, 0.f, StanceMax);
	Score += GradePoints[(int32)G];
	++Counts[(int32)G];
	if (Widget)
	{
		const FLinearColor Tint = EclipseDance::StyleColor(bStyleMatch ? PlayerStyle : OpponentStyle);
		Widget->ShowGrade(FText::FromString(GradeNames[(int32)G]), GradeColor((int32)G), Track->BeatSeconds() / Pitch);
		Widget->SetStance(OpponentStance / StanceMax, true);
		Widget->Pulse(Tint, 0.3f, bStyleMatch ? 1.8f : 1.f);
		if (bStyleMatch) Widget->FlashChain(TEXT("SAME STYLE x2"), Tint);
	}
	if (OpponentStance <= 0.f) Finish();
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
		Widget->ShowGrade(FText::FromString(bWon ? TEXT("BALANCE BROKEN") : TEXT("YOU LOSE")), bWon ? EclipseDance::Gold : EclipseUI::DialogueRed, Track ? Track->BeatSeconds() / Pitch : 0.f);
		if (bWon) Widget->ShatterStance();
	}
	EndWait = 0.f;
	// Ink owns Patience (it's a LIST), so just hand the story the result.
	if (UEclipseDialogueSubsystem* DS = GetWorld()->GetGameInstance()->GetSubsystem<UEclipseDialogueSubsystem>())
	{
		DS->SetInkInt(TEXT("dance_won"), bWon ? 1 : 0);
		if (Opponent) DS->SetInkInt(FString::Printf(TEXT("%s_dance_won"), *Opponent->DialogueId.ToString()), bWon ? 1 : 0);
	}
	bPendingDefeatTalk = bWon;
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
		for (const FKey& K : { EKeys::Up, EKeys::LeftMouseButton, EKeys::Gamepad_RightShoulder })
		{
			if (PC->WasInputKeyJustPressed(K)) { Attack(false); break; }
		}
		for (const FKey& K : { EKeys::Down, EKeys::RightMouseButton, EKeys::Gamepad_RightTrigger })
		{
			if (PC->WasInputKeyJustPressed(K)) { Attack(true); break; }
		}
		const EEclipseDanceStyle Picked = StyleFromInput(PC, PlayerStyle, bStickLatched, Unlocked);
		if (Picked != PlayerStyle)
		{
			PlayerStyle = Picked;
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
	if (!bFinished)
	{
		if (StaggeredUntil > 0.f && Song >= StaggeredUntil)
		{
			StaggeredUntil = -1.f;
			PlayerStance = 0.f;
			if (Widget) Widget->SetPlayerStance(0.f, false);
		}
		else if (StaggeredUntil < 0.f && PlayerStance > 0.f)
		{
			PlayerStance = FMath::Max(0.f, PlayerStance - DeltaTime * 14.f);   // footing comes back if you stop fluffing it
			if (Widget) Widget->SetPlayerStance(PlayerStance / PlayerStanceMax, false);
		}
	}
	if (bHeatMode && (Song >= HeatModeEnds || bFinished))
	{
		bHeatMode = false;
		Streak = 0;
		if (Wave) Wave->SetHot(false);
	}
	if (bFinished && EndWait >= 0.f)
	{
		EndWait += DeltaTime;
		if (EndWait > 4.5f) { Stop(); return; }
	}
	if (!bFinished) TickNotes(Song);
	while (!bFinished && Track && BeatIndex < FMath::FloorToInt(Beats))
	{
		++BeatIndex;
		HandleBeat(BeatIndex / Track->BeatsPerBar, BeatIndex % Track->BeatsPerBar + 1);
	}
	if (!Track) return;   // a beat can end the battle
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

	// He drifts slowly round the circle in the strafe lesson and the real battle; for the style lesson he settles back in front of you.
	const bool bStrafing = StrafeStart >= 0 && LastBar >= StrafeStart && LastBar < DemoStart;
	const bool bDrifting = bStrafing || LastBar >= BattleStart - 1;
	if (bDrifting)
	{
		// Fast tracks (over 160 BPM) move him further and quicker, up to 1.7x at 180.
		const float Fast = bTutorial ? 1.f : 1.f + 0.7f * FMath::Clamp((Track->BPM - 160.f) / 20.f, 0.f, 1.f);
		CamTimeDrift += DeltaTime * Fast * (bTutorial ? 0.5f : 1.f);
		const float Reach = bTutorial ? 16.f : 38.f;   // the tutorial keeps him within easy following distance
		OpponentDrift = FMath::Clamp(Fast * (Reach * FMath::Sin(CamTimeDrift * 0.22f) + 0.3f * Reach * (FMath::Sin(CamTimeDrift * 0.55f + 1.3f) - FMath::Sin(1.3f))), -StrafeLimitDeg + 5.f, StrafeLimitDeg - 5.f);   // starts from 0; stays where you can reach
	}
	else
	{
		OpponentDrift = FMath::FInterpTo(OpponentDrift, StrafeDeg, DeltaTime, 1.5f);
	}
	if (Widget)
	{
		Widget->SetFacingVisible(true);   // the balance bar stays up: it's how you read where he is
	}
	DriftSpeed = DeltaTime > 0.f ? (OpponentDrift - LastDrift) / DeltaTime : 0.f;
	LastDrift = OpponentDrift;
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
	if (bStrafing && FacedSeconds < HoldFacingToPass) FacedSeconds = FacingError <= FacingWindowDeg ? FacedSeconds + DeltaTime : 0.f;   // unbroken, and it sticks once passed
	// Drifting off him knocks your balance down, once the dancing has started.
	if (StaggeredUntil < 0.f && FacingError > FacingWindowDeg && LastBar >= MovesetStart) AddPlayerStance(DeltaTime * 7.f);
	if (Widget) Widget->SetFacing(Signed, FacingWindowDeg);
	CheckBalance();
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
	for (FVector2D* T : { &PlayerTilt, &OpponentTilt })
	{
		T->X = FMath::Max(0.f, T->X - DeltaTime * 5.5f);
		T->Y = FMath::Max(0.f, T->Y - DeltaTime * 3.2f);
	}
	{
	}
	if (StageLight)
	{
		const EEclipseDanceStyle S = LastBar >= 0 ? Schedule[FMath::Clamp(LastBar, 0, Schedule.Num() - 1)] : EEclipseDanceStyle::Count;
		UPointLightComponent* L = StageLight->PointLightComponent;
		L->SetLightColor(S == EEclipseDanceStyle::Count ? FLinearColor(0.6f, 0.6f, 0.8f) : EclipseDance::StyleColor(S));
		const float Tell = (TelegraphFrom > 0.f && SongSeconds(Now) >= TelegraphFrom && SongSeconds(Now) <= TelegraphTo) ? 1.f : 0.f;
		L->SetIntensity(bFinished ? 0.f : (4.f + 50.f * Backbeat + 90.f * Tell) * (bHeatMode ? 2.f : 1.f));
	}
	if (PlayerHalo)
	{
		const bool bDancing = PlayerStyle != EEclipseDanceStyle::Count && !bFinished;
		PlayerHalo->SetLightColor(bDancing ? EclipseDance::StyleColor(PlayerStyle) : FLinearColor::White);
		PlayerHalo->SetIntensity(bDancing ? 3.f + 30.f * HaloFlash : 0.f);
	}

	if (LastBar < 0 || bFinished) return;
	const float Song = SongSeconds(Now);
	const float B = (Song - Track->SegmentStartSeconds()) / Track->BeatSeconds();
	// 0 before the tell starts, 1 the moment he steps.
	const float TelegraphAlpha = (TelegraphFrom > 0.f && Song >= TelegraphFrom && Song <= TelegraphTo)
		? (Song - TelegraphFrom) / FMath::Max(0.01f, TelegraphTo - TelegraphFrom) : 0.f;

	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	for (const TPair<TWeakObjectPtr<ACharacter>, FTransform>& It : MeshBase)
	{
		ACharacter* C = It.Key.Get();
		if (!C) continue;
		const bool bIsPlayer = PC && C == PC->GetPawn();
		FVector Loc = FVector::ZeroVector;
		FRotator Rot = FRotator::ZeroRotator;
		if (bIsPlayer && StaggeredUntil > 0.f)
		{
			// Balance broken: down you go, flat out, until you're back on your feet.
			const float Down = FMath::Clamp((StaggerSeconds - (StaggeredUntil - SongSeconds(Now))) * 4.f, 0.f, 1.f);
			C->GetMesh()->SetRelativeLocationAndRotation(It.Value.GetLocation() + FVector(26.f * Down, 0.f, 6.f * Down),
				FQuat(FRotator(-82.f * Down, 0.f, 0.f)) * It.Value.GetRotation());
			continue;
		}
		const FVector2D& Tilt = bIsPlayer ? PlayerTilt : OpponentTilt;
		const float Strike = FMath::Max(Tilt.X, Tilt.Y);
		StyleMotion(bIsPlayer ? PlayerStyle : EEclipseDanceStyle::Count, B, Loc, Rot);
		// He leans and bobs into whichever way he's sliding, so you can see him go.
		if (!bIsPlayer)
		{
			Rot.Roll += FMath::Clamp(DriftSpeed * 0.6f, -14.f, 14.f);
			Loc.Y += FMath::Clamp(DriftSpeed * 0.5f, -10.f, 10.f);
			Loc.Z += 6.f * FMath::Abs(FMath::Sin(PI * B)) * FMath::Min(1.f, FMath::Abs(DriftSpeed) * 0.1f);
		}
		Loc *= 1.f - Strike;   // the swaying idle gets out of the way of an attack
		Rot.Roll *= 1.f - Strike;
		Rot.Yaw *= 1.f - Strike;
		// His tell over the two beats before he steps: a slow twist for a light lead, a lean back for a heavy one.
		if (!bIsPlayer && TelegraphAlpha > 0.f)
		{
			const float Ease = FMath::InterpEaseInOut(0.f, 1.f, TelegraphAlpha, 2.f);
			if (TelegraphDir > 0.f) Rot.Yaw += 38.f * Ease;
			else { Rot.Pitch -= 28.f * Ease; Loc.X -= 10.f * Ease; }
		}
		// Light is arms and head: it lifts, leans back a little and twists the shoulders.
		Rot.Pitch -= 12.f * Tilt.X;
		Rot.Yaw += 26.f * Tilt.X;
		Loc.Z += 28.f * Tilt.X;
		// Heavy is the feet: a stamp straight down, pitching forward over the front foot.
		Rot.Pitch += 32.f * Tilt.Y;
		Loc.Z -= 34.f * Tilt.Y;
		Loc.X += 14.f * Tilt.Y;
		// Offsets are in actor space, so pre-multiply onto the mesh's own relative rotation.
		C->GetMesh()->SetRelativeLocationAndRotation(It.Value.GetLocation() + Loc, FQuat(Rot) * It.Value.GetRotation());
	}
}
