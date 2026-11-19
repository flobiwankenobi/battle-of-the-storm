#include "Core/BotSPlayerController.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Character/BotSCharacters.h"
#include "Match/BotSMatch.h"
#include "Items/BotSItems.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"

// ---------------------------------------------------------------------------------------------
// PlayerController
// ---------------------------------------------------------------------------------------------
ABotSPlayerController::ABotSPlayerController()
{
	bShowMouseCursor = true;
	// Die Kamera gehoert dem CameraRig, nicht dem Helden.
	bAutoManageActiveCameraTarget = false;
	Vision =CreateDefaultSubobject<UBotSVisionComponent>("Vision");
}

ABotSPlayerState* ABotSPlayerController::GetBotSPlayerState() const { return GetPlayerState<ABotSPlayerState>(); }
ABotSHeroCharacter* ABotSPlayerController::GetHero() const { return Cast<ABotSHeroCharacter>(GetPawn()); }

void ABotSPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) { return; }
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);

	FActorSpawnParameters Params;
	Params.Owner = this;
	CameraRig = GetWorld()->SpawnActor<ABotSCameraRig>(ABotSCameraRig::StaticClass(), FTransform::Identity, Params);
	StormWall = GetWorld()->SpawnActor<ABotSStormWall>(ABotSStormWall::StaticClass(), FTransform::Identity, Params);
	SetViewTarget(CameraRig);
	bDemo = FParse::Param(FCommandLine::Get(), TEXT("BotSDemo"));
}

void ABotSPlayerController::DemoTick(float DeltaTime)
{
	const ABotSGameState* GS = GetWorld()->GetGameState<ABotSGameState>();
	const ABotSPlayerState* PS = GetBotSPlayerState();
	if (!GS || !PS) { return; }
	DemoTotal += DeltaTime;
	if (GS->Phase != DemoLastPhase) { DemoLastPhase = GS->Phase; DemoPhaseTime = 0.f; DemoShots = 0; }
	DemoPhaseTime += DeltaTime;
	auto Shot = [this, GS](float At, int32 Index)
	{
		if (DemoShots == Index && DemoPhaseTime >= At)
		{
			++DemoShots;
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("BotSDemo_%d_%d"), int32(GS->Phase), Index), true, false);
		}
	};
	const FBotSGameData& Data = BotSData();
	switch (GS->Phase)
	{
	case EBotSMatchPhase::HeroSelect:
		Shot(1.f, 0);
		if (DemoPhaseTime > 2.f && PS->HeroId.IsNone() && Data.Heroes.Num() > 0)
		{
			int32 Pick = FMath::RandRange(0, Data.Heroes.Num() - 1);
			FParse::Value(FCommandLine::Get(), TEXT("BotSDemoHero="), Pick);
			ServerSelectHero(Data.Heroes[FMath::Clamp(Pick, 0, Data.Heroes.Num() - 1)].Id);
		}
		Shot(3.5f, 1);
		break;
	case EBotSMatchPhase::SpawnSelect:
		if (PS->SpawnChoice < 0 && DemoPhaseTime > 0.5f) { ServerSelectSpawn(2); }
		Shot(1.2f, 0);
		break;
	case EBotSMatchPhase::Match:
		Shot(3.f, 0); Shot(12.f, 1); Shot(30.f, 2); Shot(60.f, 3); Shot(120.f, 4);
		bMoveUp = FMath::Fmod(DemoPhaseTime, 8.f) < 4.f;
		bMoveRight = FMath::Fmod(DemoPhaseTime, 6.f) < 2.f;
		bMoveDown = !bMoveUp && FMath::Fmod(DemoPhaseTime, 8.f) > 6.f;
		bAttackHeld = true;
		if (DemoPhaseTime >= DemoNextCast)
		{
			DemoNextCast = DemoPhaseTime + 1.2f;
			const EBotSSlot Slots[] = { EBotSSlot::Ability1, EBotSSlot::Ability2, EBotSSlot::Ability3, EBotSSlot::Ultimate };
			OnSlotPressed(Slots[FMath::RandRange(0, 3)]);
			if (FMath::Fmod(DemoPhaseTime, 20.f) > 15.f) { ServerInteract(true); } else { ServerInteract(false); }
			if (PS->GetOpenTalentTier() >= 0) { ServerChooseTalent(PS->GetOpenTalentTier(), 0); }
		}
		break;
	case EBotSMatchPhase::Ended:
		Shot(1.5f, 0);
		break;
	default:
		Shot(2.f, 0);
		break;
	}
	float MaxTime = 90.f;
	FParse::Value(FCommandLine::Get(), TEXT("BotSDemoTime="), MaxTime);
	if (DemoTotal >= MaxTime) { bDemo = false; ConsoleCommand(TEXT("quit")); }
}

UInputAction* ABotSPlayerController::AddAction(FName Name, FName DefaultKey)
{
	UInputAction* Action = NewObject<UInputAction>(this, Name);
	Action->ValueType = EInputActionValueType::Boolean;
	MappingContext->MapKey(Action, FKey(DefaultKey));
	Actions.Add(Name, Action);
	return Action;
}

void ABotSPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogBotS, Error, TEXT("Enhanced Input ist nicht aktiv"));
		return;
	}
	// Belegung laut GDD Abschnitt 9. Kontext IMC_Hero und Aktionen IA_* entstehen hier zur Laufzeit.
	MappingContext = NewObject<UInputMappingContext>(this, "IMC_Hero");
	auto Bind = [this, Input](FName Name, FName Key, TFunction<void(bool)> Handler)
	{
		UInputAction* Action = AddAction(Name, Key);
		Input->BindActionValueLambda(Action, ETriggerEvent::Started, [Handler](const FInputActionValue&) { Handler(true); });
		Input->BindActionValueLambda(Action, ETriggerEvent::Completed, [Handler](const FInputActionValue&) { Handler(false); });
	};
	Bind("IA_MoveUp", "W", [this](bool b) { bMoveUp = b; });
	Bind("IA_MoveDown", "S", [this](bool b) { bMoveDown = b; });
	Bind("IA_MoveLeft", "A", [this](bool b) { bMoveLeft = b; });
	Bind("IA_MoveRight", "D", [this](bool b) { bMoveRight = b; });
	Bind("IA_BasicAttack", "LeftMouseButton", [this](bool b) { OnAttack(b); });
	Bind("IA_Ability1", "One", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Ability1); } });
	Bind("IA_Ability2", "Two", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Ability2); } });
	Bind("IA_Ability3", "Three", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Ability3); } });
	Bind("IA_Ability4", "Four", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Ultimate); } });
	Bind("IA_Ability5", "Five", [this](bool b) { /* Merkmale der vier Prototyp-Helden sind passiv. */ });
	Bind("IA_Item1", "Q", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Item1); } });
	Bind("IA_Item2", "E", [this](bool b) { if (b) { OnSlotPressed(EBotSSlot::Item2); } });
	Bind("IA_Interact", "F", [this](bool b) { ServerInteract(b); });
	Bind("IA_Map", "Tab", [this](bool b) { if (b) { bShowMap = !bShowMap; } });
	Bind("IA_StormRun", "Z", [this](bool b)
	{
		ABotSHeroCharacter* Hero = GetHero();
		if (!b || !Hero) { return; }
		ServerStormRun();
		if (Hero->HasAuthority()) { Hero->GetBotSMovement()->bWantsStormRun = true; }
		else { Hero->LocalBeginStormRun(); }
	});
}

void ABotSPlayerController::OnAttack(bool bPressed)
{
	if (bPressed)
	{
		float X, Y;
		ABotSHUD* Hud = Cast<ABotSHUD>(GetHUD());
		if (Hud && GetMousePosition(X, Y) && Hud->HandleClick(FVector2D(X, Y))) { return; }
		if (ABotSHeroCharacter* Hero = GetHero()) { Hero->LocalCancelStormRun(); }
	}
	bAttackHeld = bPressed;
}

void ABotSPlayerController::OnSlotPressed(EBotSSlot Slot)
{
	ABotSPlayerState* PS = GetBotSPlayerState();
	if (!PS) { return; }
	// Strg+1 und Strg+2 waehlen ein Talent und loesen die Faehigkeit nicht aus.
	if (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl))
	{
		const int32 Tier = PS->GetOpenTalentTier();
		if (Tier >= 0 && (Slot == EBotSSlot::Ability1 || Slot == EBotSSlot::Ability2)) { ServerChooseTalent(Tier, Slot == EBotSSlot::Ability1 ? 0 : 1); }
		return;
	}
	ABotSHeroCharacter* Hero = GetHero();
	if (!Hero) { return; }
	if (Slot != EBotSSlot::Item1 && Slot != EBotSSlot::Item2) { Hero->LocalCancelStormRun(); }
	ServerUseSlot(uint8(Slot), LocalAim);
}

void ABotSPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!IsLocalController()) { return; }

	if (MappingContext)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			if (!Subsystem->HasMappingContext(MappingContext)) { Subsystem->AddMappingContext(MappingContext, 0); }
		}
	}

	if (bDemo) { DemoTick(DeltaTime); }
	ABotSHeroCharacter* Hero = GetHero();
	const ABotSPlayerState* PS = GetBotSPlayerState();

	// Zielpunkt: Schnitt des Mausstrahls mit einer waagerechten Ebene auf Fusshoehe (TDD Abschnitt 7).
	const float FootZ = Hero ? Hero->GetActorLocation().Z - Hero->GetSimpleCollisionHalfHeight() : 0.f;
	FVector Origin, Direction;
	bool bHasAim = false;
	if (DeprojectMousePositionToWorld(Origin, Direction) && FMath::Abs(Direction.Z) > KINDA_SMALL_NUMBER)
	{
		const float T = (FootZ - Origin.Z) / Direction.Z;
		if (T > 0.f) { LocalAim = Origin + Direction * T; bHasAim = true; }
	}

	if (CameraRig)
	{
		AActor* Follow = Hero;
		if (!Follow && PS && PS->Squad)
		{
			// Ausgeschaltet: Kamera folgt einem lebenden Truppmitglied.
			for (const ABotSPlayerState* Mate : PS->Squad->Members) { if (Mate && Mate->GetHero()) { Follow = Mate->GetHero(); break; } }
		}
		if (Follow) { CameraRig->FollowTarget = Follow; }
		CameraRig->AimPoint = LocalAim;
		CameraRig->bHasAim = bHasAim && Hero != nullptr;
		if (GetViewTarget() != CameraRig) { SetViewTarget(CameraRig); }
	}

	if (!Hero) { return; }
	// Bewegung entlang der Bildschirmachsen: die Kamera blickt entlang +X.
	const float Forward = (bMoveUp ? 1.f : 0.f) - (bMoveDown ? 1.f : 0.f);
	const float Right = (bMoveRight ? 1.f : 0.f) - (bMoveLeft ? 1.f : 0.f);
	if (Forward != 0.f) { Hero->AddMovementInput(FVector::ForwardVector, Forward); }
	if (Right != 0.f) { Hero->AddMovementInput(FVector::RightVector, Right); }

	if (BotSData().Tuning.bFaceCursor && bHasAim)
	{
		const FVector ToAim = LocalAim - Hero->GetActorLocation();
		if (ToAim.SizeSquared2D() > 25.f) { SetControlRotation(FRotator(0.f, ToAim.Rotation().Yaw, 0.f)); }
	}
	else if (Forward != 0.f || Right != 0.f)
	{
		SetControlRotation(FRotator(0.f, FVector(Forward, Right, 0.f).Rotation().Yaw, 0.f));
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextAimSend)
	{
		NextAimSend = Now + 0.05f;
		ServerSetAim(LocalAim, bAttackHeld);
	}
}

void ABotSPlayerController::ServerSetAim_Implementation(FVector_NetQuantize Aim, bool bAttacking)
{
	if (ABotSHeroCharacter* Hero = GetHero()) { Hero->SetAim(Aim); Hero->SetAttacking(bAttacking); }
}

void ABotSPlayerController::ServerUseSlot_Implementation(uint8 Slot, FVector_NetQuantize Aim)
{
	ABotSHeroCharacter* Hero = GetHero();
	if (Hero && Slot < uint8(EBotSSlot::Count)) { Hero->TryUseSlot(EBotSSlot(Slot), Aim); }
}

void ABotSPlayerController::ServerStormRun_Implementation() { if (ABotSHeroCharacter* Hero = GetHero()) { Hero->StartStormRun(); } }
void ABotSPlayerController::ServerInteract_Implementation(bool bHeld) { if (ABotSHeroCharacter* Hero = GetHero()) { Hero->SetInteractHeld(bHeld); } }

void ABotSPlayerController::ServerSelectHero_Implementation(FName HeroId)
{
	ABotSGameMode* GM = GetWorld()->GetAuthGameMode<ABotSGameMode>();
	if (GM && !GM->SelectHero(GetBotSPlayerState(), HeroId)) { UE_LOG(LogBotSMatch, Log, TEXT("Heldenwahl abgelehnt: %s"), *HeroId.ToString()); }
}

void ABotSPlayerController::ServerSelectSpawn_Implementation(int32 SpawnIndex)
{
	if (ABotSGameMode* GM = GetWorld()->GetAuthGameMode<ABotSGameMode>()) { GM->SelectSpawn(GetBotSPlayerState(), SpawnIndex); }
}

void ABotSPlayerController::ServerChooseTalent_Implementation(int32 Tier, int32 Option)
{
	if (ABotSPlayerState* PS = GetBotSPlayerState()) { PS->ChooseTalent(Tier, Option); }
}

void ABotSPlayerController::ServerForceStart_Implementation()
{
	if (ABotSGameMode* GM = GetWorld()->GetAuthGameMode<ABotSGameMode>()) { GM->ForceStart(); }
}

void ABotSPlayerController::BotSStart() { ServerForceStart(); }

// ---------------------------------------------------------------------------------------------
// Sicht
// ---------------------------------------------------------------------------------------------
UBotSVisionComponent::UBotSVisionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f; // 10-mal pro Sekunde
}

void UBotSVisionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ABotSPlayerController* PC = Cast<ABotSPlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) { return; }
	UWorld* World = GetWorld();
	const ABotSPlayerState* MyPS = PC->GetBotSPlayerState();
	const int32 MyTeam = MyPS ? MyPS->SquadIndex : -2;
	const float Sight = BotSM(BotSData().Tuning.SightRange);

	// Betrachter: alle Helden des eigenen Trupps, die noch im Match sind. Die Sicht ist geteilt.
	struct FViewer { FVector Location; float RangeSq; };
	TArray<FViewer, TInlineAllocator<4>> Viewers;
	for (TActorIterator<ABotSHeroCharacter> It(World); It; ++It)
	{
		const ABotSPlayerState* PS = It->GetBotSPlayerState();
		if (!PS || PS->SquadIndex != MyTeam || !PS->IsInMatch()) { continue; }
		// Im Sturm halbiert sich die Sichtweite des betroffenen Helden.
		const float Range = PS->bInStorm ? Sight * 0.5f : Sight;
		Viewers.Add({ It->GetActorLocation() + FVector(0.f, 0.f, 60.f), Range * Range });
	}
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(BotSVision), false);
	auto CanSee = [&](const FVector& Point)
	{
		if (Viewers.Num() == 0) { return true; } // Zuschauer ohne Trupp sehen alles.
		for (const FViewer& V : Viewers)
		{
			if (FVector::DistSquared2D(V.Location, Point) > V.RangeSq) { continue; }
			if (!World->LineTraceTestByChannel(V.Location, FVector(Point.X, Point.Y, V.Location.Z), ECC_Visibility, Query)) { return true; }
		}
		return false;
	};

	for (TActorIterator<ABotSCombatCharacter> It(World); It; ++It) { It->SetActorHiddenInGame(It->GetTeam() != MyTeam && !CanSee(It->GetActorLocation())); }
	for (TActorIterator<ABotSProjectile> It(World); It; ++It) { It->SetActorHiddenInGame(It->Team != MyTeam && !CanSee(It->GetActorLocation())); }
	for (TActorIterator<ABotSAreaEffect> It(World); It; ++It) { It->SetActorHiddenInGame(It->Team != MyTeam && !CanSee(It->GetActorLocation())); }
}
