#include "Character/BotSCharacters.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Match/BotSMatch.h"
#include "Items/BotSItems.h"
#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------------------------
// Basis
// ---------------------------------------------------------------------------------------------
ABotSCombatCharacter::ABotSCombatCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));

	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>("Body");
	BodyMesh->SetupAttachment(RootComponent);
	BodyMesh->SetStaticMesh(Cylinder.Object);
	BodyMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.7f));
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NoseMesh = CreateDefaultSubobject<UStaticMeshComponent>("Nose");
	NoseMesh->SetupAttachment(RootComponent);
	NoseMesh->SetStaticMesh(Cube.Object);
	NoseMesh->SetRelativeLocation(FVector(50.f, 0.f, 45.f));
	NoseMesh->SetRelativeScale3D(FVector(0.5f, 0.2f, 0.2f));
	NoseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	bReplicates = true;
}

float ABotSCombatCharacter::GetHealth() const { const UBotSAttributeSet* A = GetAttributes(); return A ? A->GetHealth() : 0.f; }
float ABotSCombatCharacter::GetMaxHealth() const { const UBotSAttributeSet* A = GetAttributes(); return A ? A->GetMaxHealth() : 1.f; }
float ABotSCombatCharacter::GetShield() const { const UBotSAttributeSet* A = GetAttributes(); return A ? A->GetShield() : 0.f; }

bool ABotSCombatCharacter::HasTag(const FGameplayTag& Tag) const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(Tag);
}

void ABotSCombatCharacter::Shove(const FVector& Direction, float DistanceM)
{
	if (!HasAuthority() || DistanceM <= 0.f) { return; }
	const FVector Dir = Direction.GetSafeNormal2D();
	SetActorLocation(GetActorLocation() + Dir * BotSM(DistanceM), true);
}

void ABotSCombatCharacter::SetBodyColor(const FLinearColor& Color)
{
	BotSCombat::SetMeshColor(BodyMesh, Color);
	BotSCombat::SetMeshColor(NoseMesh, FLinearColor(0.05f, 0.05f, 0.05f));
}

// ---------------------------------------------------------------------------------------------
// Bewegung
// ---------------------------------------------------------------------------------------------
class FBotSSavedMove : public FSavedMove_Character
{
public:
	uint8 bSavedWantsStormRun : 1 = false;

	virtual void Clear() override { FSavedMove_Character::Clear(); bSavedWantsStormRun = false; }
	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Result = FSavedMove_Character::GetCompressedFlags();
		if (bSavedWantsStormRun) { Result |= FLAG_Custom_0; }
		return Result;
	}
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		if (bSavedWantsStormRun != static_cast<const FBotSSavedMove*>(NewMove.Get())->bSavedWantsStormRun) { return false; }
		return FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		if (const UBotSCharacterMovementComponent* Move = Cast<UBotSCharacterMovementComponent>(Character->GetCharacterMovement())) { bSavedWantsStormRun = Move->bWantsStormRun; }
	}
	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		if (UBotSCharacterMovementComponent* Move = Cast<UBotSCharacterMovementComponent>(Character->GetCharacterMovement())) { Move->bWantsStormRun = bSavedWantsStormRun; }
	}
};

class FBotSPredictionData : public FNetworkPredictionData_Client_Character
{
public:
	explicit FBotSPredictionData(const UCharacterMovementComponent& Movement) : FNetworkPredictionData_Client_Character(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FBotSSavedMove()); }
};

FNetworkPredictionData_Client* UBotSCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UBotSCharacterMovementComponent* Mutable = const_cast<UBotSCharacterMovementComponent*>(this);
		Mutable->ClientPredictionData = new FBotSPredictionData(*this);
	}
	return ClientPredictionData;
}

void UBotSCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsStormRun = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

float UBotSCharacterMovementComponent::GetMaxSpeed() const
{
	const ABotSHeroCharacter* Hero = Cast<ABotSHeroCharacter>(CharacterOwner);
	const UBotSAttributeSet* Attr = Hero ? Hero->GetAttributes() : nullptr;
	if (!Attr) { return Super::GetMaxSpeed(); }
	if (Hero->HasTag(TAG_State_Stunned) || Hero->HasTag(TAG_State_Rooted)) { return 0.f; }
	if (Hero->IsDowned()) { return BotSM(BotSData().MatchRules.CrawlSpeed); }

	float Speed = BotSM(Attr->GetMoveSpeed());
	// Eigener Client: sagt den Sturmlauf voraus. Server: nur mit Freigabe. Andere Clients: replizierte Freigabe.
	bool bRun;
	switch (Hero->GetLocalRole())
	{
	case ROLE_AutonomousProxy: bRun = bWantsStormRun; break;
	case ROLE_Authority: bRun = bWantsStormRun && Hero->bStormRunAllowed; break;
	default: bRun = Hero->bStormRunAllowed; break;
	}
	if (bRun) { Speed *= BotSData().Tuning.StormRunSpeedMul; }
	return Speed;
}

// ---------------------------------------------------------------------------------------------
// Held
// ---------------------------------------------------------------------------------------------
ABotSHeroCharacter::ABotSHeroCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UBotSCharacterMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationYaw = true;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->MaxAcceleration = 6000.f;
	Move->BrakingDecelerationWalking = 6000.f;
	Move->GroundFriction = 10.f;
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void ABotSHeroCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSHeroCharacter, bStormRunAllowed);
	DOREPLIFETIME(ABotSHeroCharacter, ReviveEndTime);
	DOREPLIFETIME(ABotSHeroCharacter, ReviveDuration);
	DOREPLIFETIME(ABotSHeroCharacter, StormRunChannelEnd);
}

ABotSPlayerState* ABotSHeroCharacter::GetBotSPlayerState() const { return GetPlayerState<ABotSPlayerState>(); }
UBotSCharacterMovementComponent* ABotSHeroCharacter::GetBotSMovement() const { return Cast<UBotSCharacterMovementComponent>(GetCharacterMovement()); }

UAbilitySystemComponent* ABotSHeroCharacter::GetAbilitySystemComponent() const
{
	const ABotSPlayerState* PS = GetBotSPlayerState();
	return PS ? PS->GetAbilitySystemComponent() : nullptr;
}

const UBotSAttributeSet* ABotSHeroCharacter::GetAttributes() const
{
	const ABotSPlayerState* PS = GetBotSPlayerState();
	return PS ? PS->GetAttributes() : nullptr;
}

int32 ABotSHeroCharacter::GetTeam() const { const ABotSPlayerState* PS = GetBotSPlayerState(); return PS ? PS->SquadIndex : -1; }
bool ABotSHeroCharacter::IsTargetable() const { const ABotSPlayerState* PS = GetBotSPlayerState(); return PS && PS->IsInMatch(); }
bool ABotSHeroCharacter::IsDowned() const { const ABotSPlayerState* PS = GetBotSPlayerState(); return PS && PS->LifeState == EBotSLifeState::Downed; }

void ABotSHeroCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitFromPlayerState();
}

void ABotSHeroCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitFromPlayerState();
}

void ABotSHeroCharacter::InitFromPlayerState()
{
	if (ABotSPlayerState* PS = GetBotSPlayerState())
	{
		PS->GetASC()->InitAbilityActorInfo(PS, this);
		bAppearanceSet = false;
	}
}

void ABotSHeroCharacter::RefreshAppearance()
{
	const ABotSPlayerState* PS = GetBotSPlayerState();
	const FBotSHeroDef* Def = PS ? PS->GetHeroDef() : nullptr;
	if (Def)
	{
		SetBodyColor(Def->Color);
		bAppearanceSet = true;
	}
}

void ABotSHeroCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAppearanceSet && GetNetMode() != NM_DedicatedServer) { RefreshAppearance(); }
	// Niedergeschlagene Helden liegen flach.
	BodyMesh->SetRelativeScale3D(IsDowned() ? FVector(0.8f, 0.8f, 0.5f) : FVector(0.8f, 0.8f, 1.7f));
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, IsDowned() ? -60.f : 0.f));

	if (IsLocallyControlled() && !HasAuthority())
	{
		UBotSCharacterMovementComponent* Move = GetBotSMovement();
		const float Now = GetWorld()->GetTimeSeconds();
		if (LocalStormRunStart > 0.f && Now - LocalStormRunStart >= BotSData().Tuning.StormRunChannel)
		{
			LocalStormRunStart = 0.f;
			LocalStormRunWantTime = Now;
			Move->bWantsStormRun = true;
		}
		// Der Server hat den Sturmlauf nicht freigegeben: Vorhersage zuruecknehmen.
		if (Move->bWantsStormRun && !bStormRunAllowed && Now - LocalStormRunWantTime > 1.f) { Move->bWantsStormRun = false; }
	}
	if (HasAuthority()) { ServerTick(DeltaSeconds); }
}

void ABotSHeroCharacter::LocalBeginStormRun()
{
	UBotSCharacterMovementComponent* Move = GetBotSMovement();
	if (Move->bWantsStormRun || LocalStormRunStart > 0.f || IsDowned()) { return; }
	LocalStormRunStart = GetWorld()->GetTimeSeconds();
}

void ABotSHeroCharacter::LocalCancelStormRun()
{
	LocalStormRunStart = 0.f;
	GetBotSMovement()->bWantsStormRun = false;
}

void ABotSHeroCharacter::OnRep_StormRunAllowed()
{
	if (!bStormRunAllowed) { LocalCancelStormRun(); }
}

void ABotSHeroCharacter::ServerTick(float DeltaSeconds)
{
	ABotSPlayerState* PS = GetBotSPlayerState();
	if (!PS) { return; }
	if (!PS->IsAlive())
	{
		CancelStormRun();
		ReviveTarget = nullptr;
		ReviveEndTime = 0.f;
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (StormRunChannelEnd > 0.f && Now >= StormRunChannelEnd)
	{
		StormRunChannelEnd = 0.f;
		bStormRunAllowed = true;
	}
	if (bAttacking) { TryUseSlot(EBotSSlot::Basic, AimPoint); }
	TickRevive(DeltaSeconds);
}

bool ABotSHeroCharacter::TryUseSlot(EBotSSlot Slot, const FVector& Aim)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!HasAuthority() || !ASC) { return false; }
	AimPoint = Aim;
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromInputID(int32(Slot));
	return Spec && ASC->TryActivateAbility(Spec->Handle);
}

void ABotSHeroCharacter::StartStormRun()
{
	const ABotSPlayerState* PS = GetBotSPlayerState();
	if (!PS || !PS->IsAlive() || bStormRunAllowed || StormRunChannelEnd > 0.f) { return; }
	if (HasTag(TAG_State_Stunned) || HasTag(TAG_State_Rooted)) { return; }
	StormRunChannelEnd = GetWorld()->GetTimeSeconds() + BotSData().Tuning.StormRunChannel;
}

void ABotSHeroCharacter::CancelStormRun()
{
	bStormRunAllowed = false;
	StormRunChannelEnd = 0.f;
}

float ABotSHeroCharacter::NotifyAbilityUsed(EBotSSlot Slot)
{
	const bool bItem = Slot == EBotSSlot::Item1 || Slot == EBotSSlot::Item2;
	if (!bItem)
	{
		CancelStormRun();
		ReviveTarget = nullptr;
		ReviveEndTime = 0.f;
	}
	const ABotSPlayerState* PS = GetBotSPlayerState();
	const FBotSHeroDef* Def = PS ? PS->GetHeroDef() : nullptr;
	if (PS) { const_cast<ABotSPlayerState*>(PS)->NotifyCombat(); }
	if (Def && Def->TraitType == "CritAfterAbility")
	{
		const float Now = GetWorld()->GetTimeSeconds();
		if (Slot == EBotSSlot::Basic)
		{
			if (Now < CritReadyUntil) { CritReadyUntil = 0.f; return 1.f + Def->Trait("Bonus", 1.f); }
		}
		else if (!bItem)
		{
			CritReadyUntil = Now + Def->Trait("Window", 4.f);
		}
	}
	return 1.f;
}

void ABotSHeroCharacter::NotifyDamaged()
{
	// Sturmlauf endet bei Schaden (GDD Abschnitt 6), Wiederbelebung wird unterbrochen (GDD Abschnitt 10).
	CancelStormRun();
	ReviveTarget = nullptr;
	ReviveEndTime = 0.f;
}

void ABotSHeroCharacter::SetInteractHeld(bool bHeld)
{
	const bool bPressed = bHeld && !bInteractHeld;
	bInteractHeld = bHeld;
	if (!bHeld)
	{
		ReviveTarget = nullptr;
		ReviveEndTime = 0.f;
	}
	else if (bPressed)
	{
		TryInteract();
	}
}

void ABotSHeroCharacter::TryInteract()
{
	ABotSPlayerState* PS = GetBotSPlayerState();
	if (!PS || !PS->IsAlive()) { return; }
	const FBotSGameData& Data = BotSData();
	const FVector Loc = GetActorLocation();
	const float Now = GetWorld()->GetTimeSeconds();

	// 1. Niedergeschlagenes Truppmitglied wiederbeleben.
	const float ReviveRange = BotSM(Data.MatchRules.ReviveRange);
	for (TActorIterator<ABotSHeroCharacter> It(GetWorld()); It; ++It)
	{
		ABotSHeroCharacter* Other = *It;
		if (Other == this || !Other->IsDowned() || Other->GetTeam() != GetTeam()) { continue; }
		if (FVector::DistSquared2D(Other->GetActorLocation(), Loc) > ReviveRange * ReviveRange) { continue; }
		const ABotSPlayerState* OtherPS = Other->GetBotSPlayerState();
		float Duration = Data.MatchRules.ReviveTime / (1.f + PS->GetStatBonus("ReviveSpeed"));
		// Wirkung im Sturm: Wiederbelebung dauert doppelt so lange.
		if (PS->bInStorm || (OtherPS && OtherPS->bInStorm)) { Duration *= 2.f; }
		CancelStormRun();
		ReviveTarget = Other;
		ReviveStartLocation = Loc;
		ReviveDuration = Duration;
		ReviveEndTime = Now + Duration;
		return;
	}

	// 2. Gegenstand aufheben.
	const float Range = BotSM(Data.Tuning.InteractRange);
	ABotSPickup* BestPickup = nullptr;
	float BestDist = Range * Range;
	for (TActorIterator<ABotSPickup> It(GetWorld()); It; ++It)
	{
		const float D = FVector::DistSquared2D(It->GetActorLocation(), Loc);
		if (D <= BestDist) { BestDist = D; BestPickup = *It; }
	}
	if (BestPickup)
	{
		FBotSItemSlot Dropped;
		if (PS->GetInventory()->AddItem(BestPickup->ItemId, BestPickup->Charges, Dropped))
		{
			UE_LOG(LogBotSItems, Log, TEXT("%s hebt %s auf"), *PS->GetPlayerName(), *BestPickup->ItemId.ToString());
			const FVector PickupLoc = BestPickup->GetActorLocation();
			BestPickup->Destroy();
			if (!Dropped.Item.IsNone()) { ABotSPickup::SpawnPickup(GetWorld(), PickupLoc, Dropped.Item, Dropped.Charges); }
			PS->RecomputeStats();
		}
		return;
	}

	// 3. Truhe oeffnen.
	for (TActorIterator<ABotSLootChest> It(GetWorld()); It; ++It)
	{
		if (!It->bOpened && FVector::DistSquared2D(It->GetActorLocation(), Loc) <= Range * Range)
		{
			It->Open();
			return;
		}
	}
}

void ABotSHeroCharacter::TickRevive(float DeltaSeconds)
{
	if (ReviveEndTime <= 0.f) { return; }
	ABotSHeroCharacter* Target = ReviveTarget.Get();
	const float Range = BotSM(BotSData().MatchRules.ReviveRange) + 50.f;
	const bool bValid = Target && Target->IsDowned() && bInteractHeld
		&& FVector::DistSquared2D(Target->GetActorLocation(), GetActorLocation()) <= Range * Range
		&& FVector::DistSquared2D(ReviveStartLocation, GetActorLocation()) <= 40.f * 40.f;
	if (!bValid)
	{
		ReviveTarget = nullptr;
		ReviveEndTime = 0.f;
		return;
	}
	if (GetWorld()->GetTimeSeconds() >= ReviveEndTime)
	{
		if (ABotSPlayerState* TargetPS = Target->GetBotSPlayerState()) { TargetPS->Revive(); }
		ReviveTarget = nullptr;
		ReviveEndTime = 0.f;
	}
}

// ---------------------------------------------------------------------------------------------
// Kreatur
// ---------------------------------------------------------------------------------------------
ABotSCreature::ABotSCreature(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	AbilitySystem = CreateDefaultSubobject<UBotSAbilitySystemComponent>("AbilitySystem");
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	Attributes = CreateDefaultSubobject<UBotSAttributeSet>("Attributes");
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
	BodyMesh->SetRelativeScale3D(FVector(1.f, 1.f, 1.2f));
}

UAbilitySystemComponent* ABotSCreature::GetAbilitySystemComponent() const { return AbilitySystem; }

void ABotSCreature::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystem->InitAbilityActorInfo(this, this);
	SetBodyColor(FLinearColor(0.75f, 0.6f, 0.1f));
	if (HasAuthority())
	{
		const FBotSCreatureDef& Def = BotSData().Creature;
		Home = GetActorLocation();
		AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetMaxHealthAttribute(), Def.Health);
		AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), Def.Health);
		AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetMoveSpeedAttribute(), Def.MoveSpeed);
		AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetAttackSpeedAttribute(), 1.f);
		GetCharacterMovement()->MaxWalkSpeed = BotSM(Def.MoveSpeed);
		Attributes->OnDamaged.AddUObject(this, &ABotSCreature::HandleDamaged);
		Attributes->OnOutOfHealth.AddUObject(this, &ABotSCreature::HandleOutOfHealth);
	}
}

void ABotSCreature::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || bDead) { return; }
	const FBotSCreatureDef& Def = BotSData().Creature;
	const FVector Loc = GetActorLocation();
	const float Now = GetWorld()->GetTimeSeconds();
	// Verlangsamungen wirken ueber das Attribut, Betaeubung und Festhalten halten die Kreatur an.
	const bool bHeld = HasTag(TAG_State_Stunned) || HasTag(TAG_State_Rooted);
	GetCharacterMovement()->MaxWalkSpeed = bHeld ? 0.f : BotSM(Attributes->GetMoveSpeed());

	if (bReturning)
	{
		if (FVector::DistSquared2D(Loc, Home) < 120.f * 120.f)
		{
			bReturning = false;
			AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), Attributes->GetMaxHealth());
		}
		else
		{
			AddMovementInput((Home - Loc).GetSafeNormal2D());
		}
		return;
	}

	const float Leash = BotSM(Def.LeashRange);
	ABotSCombatCharacter* T = Target.Get();
	if (T && (!T->IsTargetable() || T->IsDowned() || FVector::DistSquared2D(T->GetActorLocation(), Home) > Leash * Leash)) { T = nullptr; }
	if (!T)
	{
		// Naechsten Helden in Reichweite des Lagers suchen.
		float Best = FMath::Square(BotSM(Def.AggroRange));
		for (TActorIterator<ABotSHeroCharacter> It(GetWorld()); It; ++It)
		{
			if (!It->IsTargetable() || It->IsDowned()) { continue; }
			const float D = FVector::DistSquared2D(It->GetActorLocation(), Home);
			if (D < Best) { Best = D; T = *It; }
		}
	}
	Target = T;
	if (!T)
	{
		if (FVector::DistSquared2D(Loc, Home) > 150.f * 150.f) { bReturning = true; }
		return;
	}

	const float Dist = FVector::Dist2D(T->GetActorLocation(), Loc);
	if (Dist > BotSM(Def.AttackRange))
	{
		AddMovementInput((T->GetActorLocation() - Loc).GetSafeNormal2D());
	}
	else if (Now >= NextAttackTime && !HasTag(TAG_State_Stunned))
	{
		NextAttackTime = Now + Def.AttackInterval;
		BotSCombat::ApplyDamage(this, T, Def.Damage);
	}
}

void ABotSCreature::HandleDamaged(float Damage, AActor* InstigatorActor, bool bStorm)
{
	ABotSCombatCharacter* Attacker = Cast<ABotSCombatCharacter>(InstigatorActor);
	if (!Target.IsValid() && Attacker && Attacker != this)
	{
		Target = Attacker;
		bReturning = false;
	}
}

void ABotSCreature::HandleOutOfHealth(AActor* InstigatorActor)
{
	if (bDead) { return; }
	bDead = true;
	const FBotSGameData& Data = BotSData();
	const ABotSHeroCharacter* Killer = Cast<ABotSHeroCharacter>(InstigatorActor);
	ABotSPlayerState* KillerPS = Killer ? Killer->GetBotSPlayerState() : nullptr;
	if (KillerPS && KillerPS->Squad) { KillerPS->Squad->AddXp(Data.MatchRules.CreatureXp); }
	if (FMath::FRand() < Data.MatchRules.CampLootChance)
	{
		FRandomStream Rng(FMath::Rand());
		const FName Item = FBotSRules::RollLoot(Data.CampLoot, Rng);
		const FBotSItemDef* Def = Data.FindItem(Item);
		if (Def) { ABotSPickup::SpawnPickup(GetWorld(), GetActorLocation(), Item, Def->Charges); }
	}
	SetActorEnableCollision(false);
	SetLifeSpan(0.05f);
}

// ---------------------------------------------------------------------------------------------
// Kamera
// ---------------------------------------------------------------------------------------------
ABotSCameraRig::ABotSCameraRig()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	Camera = CreateDefaultSubobject<UCameraComponent>("Camera");
	RootComponent = Camera;
}

void ABotSCameraRig::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FBotSTuning& Tuning = BotSData().Tuning;
	Camera->SetFieldOfView(Tuning.CameraFOV);
	const AActor* Target = FollowTarget.Get();
	if (!Target && bSnapped) { return; }
	const FVector Base = Target ? Target->GetActorLocation() : FVector::ZeroVector;

	// Vorlauf: begrenzter Anteil in Richtung Mauszeiger, weich nachgefuehrt.
	FVector Lead = FVector::ZeroVector;
	if (Target && bHasAim)
	{
		Lead = (AimPoint - Base) * Tuning.CameraLead;
		Lead.Z = 0.f;
		Lead = Lead.GetClampedToMaxSize(BotSM(Tuning.CameraLeadMax));
	}
	LeadOffset = bSnapped ? FMath::VInterpTo(LeadOffset, Lead, DeltaSeconds, 6.f) : Lead;
	bSnapped = true;

	const FRotator Rot(Tuning.CameraPitch, 0.f, 0.f);
	const FVector Focus = Base + LeadOffset;
	SetActorLocationAndRotation(Focus - Rot.Vector() * BotSM(Tuning.CameraDistance), Rot);
}
