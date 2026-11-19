#include "Match/BotSMatch.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Character/BotSCharacters.h"
#include "Items/BotSItems.h"
#include "Core/BotSPlayerController.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------------
// PlayerState
// ---------------------------------------------------------------------------------------------
ABotSPlayerState::ABotSPlayerState()
{
	AbilitySystem = CreateDefaultSubobject<UBotSAbilitySystemComponent>("AbilitySystem");
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	Attributes = CreateDefaultSubobject<UBotSAttributeSet>("Attributes");
	Inventory = CreateDefaultSubobject<UBotSInventoryComponent>("Inventory");
	// Die Faehigkeiten-Komponente liegt hier, deshalb hoehere Aktualisierungsrate als der Engine-Standard.
	SetNetUpdateFrequency(30.f);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ABotSPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSPlayerState, HeroId);
	DOREPLIFETIME(ABotSPlayerState, SquadIndex);
	DOREPLIFETIME(ABotSPlayerState, Squad);
	DOREPLIFETIME(ABotSPlayerState, LifeState);
	DOREPLIFETIME(ABotSPlayerState, DownedEndTime);
	DOREPLIFETIME(ABotSPlayerState, TalentChoices);
	DOREPLIFETIME(ABotSPlayerState, SpawnChoice);
	DOREPLIFETIME(ABotSPlayerState, bInStorm);
}

UAbilitySystemComponent* ABotSPlayerState::GetAbilitySystemComponent() const { return AbilitySystem; }
ABotSHeroCharacter* ABotSPlayerState::GetHero() const { return Cast<ABotSHeroCharacter>(GetPawn()); }
const FBotSHeroDef* ABotSPlayerState::GetHeroDef() const { return BotSData().FindHero(HeroId); }
int32 ABotSPlayerState::GetLevel() const { return Squad ? Squad->Level : 1; }
float ABotSPlayerState::GetLevelFactor() const { return FBotSRules::LevelFactor(GetLevel(), BotSData().MatchRules.LevelScaling); }

bool ABotSPlayerState::GetAbilityDef(EBotSSlot Slot, FBotSAbilityDef& Out) const
{
	if (Slot == EBotSSlot::Item1 || Slot == EBotSSlot::Item2)
	{
		const FBotSItemDef* Item = Inventory->GetActiveDef(Slot == EBotSSlot::Item1 ? 0 : 1);
		if (!Item) { return false; }
		Out = Item->Ability;
		return Out.IsValid();
	}
	const FBotSHeroDef* Def = GetHeroDef();
	if (!Def) { return false; }
	switch (Slot)
	{
	case EBotSSlot::Basic: Out = Def->Basic; break;
	case EBotSSlot::Ability1: Out = Def->Ability1; break;
	case EBotSSlot::Ability2: Out = Def->Ability2; break;
	case EBotSSlot::Ability3: Out = Def->Ability3; break;
	case EBotSSlot::Ultimate: Out = Def->Ultimate; break;
	default: return false;
	}
	// Talente veraendern die Werte der Faehigkeit.
	for (int32 Tier = 0; Tier < Def->Talents.Num() && Tier < TalentChoices.Num(); ++Tier)
	{
		const int32 Choice = TalentChoices[Tier];
		if (!Def->Talents[Tier].Options.IsValidIndex(Choice)) { continue; }
		for (const FBotSTalentMod& Mod : Def->Talents[Tier].Options[Choice].Mods)
		{
			if (Mod.Ability != Out.Id) { continue; }
			if (Mod.Param == "Cooldown") { Out.Cooldown = FMath::Max(0.5f, FBotSRules::ApplyOp(Out.Cooldown, Mod.Op, Mod.Value)); }
			else if (Mod.Param == "ManaCost") { Out.ManaCost = FMath::Max(0.f, FBotSRules::ApplyOp(Out.ManaCost, Mod.Op, Mod.Value)); }
			else { float& V = Out.Params.FindOrAdd(Mod.Param); V = FBotSRules::ApplyOp(V, Mod.Op, Mod.Value); }
		}
	}
	return Out.IsValid();
}

int32 ABotSPlayerState::GetOpenTalentTier() const
{
	const FBotSHeroDef* Def = GetHeroDef();
	if (!Def || !IsInMatch()) { return -1; }
	for (int32 Tier = 0; Tier < Def->Talents.Num() && Tier < TalentChoices.Num(); ++Tier)
	{
		if (TalentChoices[Tier] < 0 && GetLevel() >= Def->Talents[Tier].Level) { return Tier; }
	}
	return -1;
}

float ABotSPlayerState::GetStatBonus(FName Stat) const
{
	float Sum = Inventory->GetStat(Stat);
	if (const FBotSHeroDef* Def = GetHeroDef())
	{
		for (int32 Tier = 0; Tier < Def->Talents.Num() && Tier < TalentChoices.Num(); ++Tier)
		{
			const int32 Choice = TalentChoices[Tier];
			if (!Def->Talents[Tier].Options.IsValidIndex(Choice)) { continue; }
			for (const FBotSTalentMod& Mod : Def->Talents[Tier].Options[Choice].Mods)
			{
				if (Mod.Ability.IsNone() && Mod.Param == Stat) { Sum += Mod.Value; }
			}
		}
	}
	return Sum;
}

float ABotSPlayerState::GetCooldownRemaining(EBotSSlot Slot, float& OutDuration) const
{
	OutDuration = 0.f;
	float Remaining = 0.f;
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(BotSCooldownTag(Slot)));
	for (const TPair<float, float>& Entry : AbilitySystem->GetActiveEffectsTimeRemainingAndDuration(Query))
	{
		if (Entry.Key > Remaining) { Remaining = Entry.Key; OutDuration = Entry.Value; }
	}
	return Remaining;
}

void ABotSPlayerState::InitForMatch()
{
	const FBotSHeroDef* Def = GetHeroDef();
	if (!HasAuthority() || !Def) { return; }
	if (!bAbilitiesGranted)
	{
		bAbilitiesGranted = true;
		Attributes->OnDamaged.AddUObject(this, &ABotSPlayerState::HandleDamaged);
		Attributes->OnOutOfHealth.AddUObject(this, &ABotSPlayerState::HandleOutOfHealth);
		for (EBotSSlot Slot : { EBotSSlot::Basic, EBotSSlot::Ability1, EBotSSlot::Ability2, EBotSSlot::Ability3, EBotSSlot::Ultimate, EBotSSlot::Item1, EBotSSlot::Item2 })
		{
			AbilitySystem->GiveAbility(FGameplayAbilitySpec(UBotSGameplayAbility::StaticClass(), 1, int32(Slot), this));
		}
	}
	TalentChoices.Init(-1, Def->Talents.Num());
	Inventory->Clear();
	LifeState = EBotSLifeState::Alive;
	bInStorm = false;
	HotRemaining = 0.f;
	RecomputeStats();
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), Attributes->GetMaxHealth());
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetManaAttribute(), Attributes->GetMaxMana());
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetShieldAttribute(), 0.f);
}

void ABotSPlayerState::RecomputeStats()
{
	const FBotSHeroDef* Def = GetHeroDef();
	if (!HasAuthority() || !Def) { return; }
	const FBotSGameData& Data = BotSData();
	const FBotSRoleStats& RoleStats = Data.FindRole(Def->Role);
	const float LevelFactor = GetLevelFactor();
	const float OldMax = Attributes->GetMaxHealth(), OldHealth = Attributes->GetHealth();

	float AttackSpeedBonus = GetStatBonus("AttackSpeed");
	if (bInStorm && Def->TraitType == "StormAttackSpeed") { AttackSpeedBonus += Def->Trait("Bonus", 0.2f); }

	auto Set = [this](const FGameplayAttribute& Attribute, float Value) { AbilitySystem->SetNumericAttributeBase(Attribute, Value); };
	const float NewMax = RoleStats.Health * LevelFactor * (1.f + GetStatBonus("MaxHealth"));
	Set(UBotSAttributeSet::GetMaxHealthAttribute(), NewMax);
	Set(UBotSAttributeSet::GetAttackDamageAttribute(), RoleStats.AttackDamage * LevelFactor * (1.f + GetStatBonus("AttackDamage")));
	Set(UBotSAttributeSet::GetAttackSpeedAttribute(), RoleStats.AttackSpeed * (1.f + AttackSpeedBonus));
	Set(UBotSAttributeSet::GetAttackRangeAttribute(), RoleStats.AttackRange);
	Set(UBotSAttributeSet::GetMoveSpeedAttribute(), Data.Tuning.MoveSpeed * (1.f + GetStatBonus("MoveSpeed")));
	Set(UBotSAttributeSet::GetCooldownReductionAttribute(), GetStatBonus("CooldownReduction"));
	Set(UBotSAttributeSet::GetMaxManaAttribute(), Data.Tuning.BaseMana);
	Set(UBotSAttributeSet::GetManaRegenAttribute(), Data.Tuning.ManaRegen * (1.f + GetStatBonus("ManaRegen")));
	Set(UBotSAttributeSet::GetHealthRegenAttribute(), Data.Tuning.HealthRegen * (1.f + GetStatBonus("HealthRegen")));
	// Anteil der Lebenspunkte bleibt bei einer Aenderung des Maximums erhalten.
	if (OldMax > 0.f && LifeState == EBotSLifeState::Alive) { Set(UBotSAttributeSet::GetHealthAttribute(), OldHealth / OldMax * NewMax); }
}

bool ABotSPlayerState::ChooseTalent(int32 Tier, int32 Option)
{
	const FBotSHeroDef* Def = GetHeroDef();
	const bool bValid = HasAuthority() && Def && IsInMatch() && Def->Talents.IsValidIndex(Tier) && TalentChoices.IsValidIndex(Tier)
		&& TalentChoices[Tier] < 0 && Def->Talents[Tier].Options.IsValidIndex(Option) && GetLevel() >= Def->Talents[Tier].Level;
	if (!bValid)
	{
		UE_LOG(LogBotSMatch, Warning, TEXT("Talentwahl abgelehnt: %s Stufe %d Option %d"), *GetPlayerName(), Tier, Option);
		return false;
	}
	TalentChoices[Tier] = int8(Option);
	RecomputeStats();
	UE_LOG(LogBotSMatch, Log, TEXT("%s waehlt Talent %s"), *GetPlayerName(), *Def->Talents[Tier].Options[Option].Id.ToString());
	return true;
}

void ABotSPlayerState::AddShield(float Amount, float Duration)
{
	if (!HasAuthority() || Amount <= 0.f || !IsAlive()) { return; }
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetShieldAttribute(), Attributes->GetShield() + Amount);
	ShieldExpireTime = FMath::Max(ShieldExpireTime, float(GetWorld()->GetTimeSeconds()) + Duration);
}

void ABotSPlayerState::AddHealOverTime(float Total, float Duration)
{
	HotRemaining += Total;
	HotRate = HotRemaining / FMath::Max(0.5f, Duration);
}

void ABotSPlayerState::SetInStorm(bool bNew)
{
	if (bInStorm == bNew) { return; }
	bInStorm = bNew;
	if (bNew) { AbilitySystem->AddLooseGameplayTag(TAG_State_InStorm); }
	else { AbilitySystem->RemoveLooseGameplayTag(TAG_State_InStorm); }
	RecomputeStats();
}

void ABotSPlayerState::NotifyCombat()
{
	const float Now = GetWorld()->GetTimeSeconds();
	// Relikt "Schild bei Kampfbeginn": Kampf beginnt nach 8 s ohne Kampfhandlung neu.
	if (Now - LastCombatTime > 8.f && IsAlive())
	{
		const float CombatShield = Inventory->GetStat("CombatShield");
		if (CombatShield > 0.f) { AddShield(CombatShield, 5.f); }
	}
	LastCombatTime = Now;
}

void ABotSPlayerState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }
	const float Now = GetWorld()->GetTimeSeconds();
	const FBotSMatchRules& Rules = BotSData().MatchRules;

	if (LifeState == EBotSLifeState::Alive)
	{
		if (Attributes->GetShield() > 0.f && Now >= ShieldExpireTime)
		{
			AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetShieldAttribute(), 0.f);
		}
		RegenAccum += DeltaSeconds;
		if (RegenAccum >= 0.5f)
		{
			RegenAccum -= 0.5f;
			// Wirkung im Sturm: keine Lebensregeneration.
			if (!bInStorm)
			{
				AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), FMath::Min(Attributes->GetMaxHealth(), Attributes->GetHealth() + Attributes->GetHealthRegen() * 0.5f));
			}
			AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetManaAttribute(), FMath::Min(Attributes->GetMaxMana(), Attributes->GetMana() + Attributes->GetManaRegen() * 0.5f));
			if (HotRemaining > 0.f)
			{
				const float Amount = FMath::Min(HotRemaining, HotRate * 0.5f);
				HotRemaining -= Amount;
				BotSCombat::ApplyHeal(GetHero(), GetHero(), Amount);
			}
		}
	}
	else if (LifeState == EBotSLifeState::Downed)
	{
		// Ausbluten: der Vorrat an Lebenspunkten laeuft ueber die Dauer des Niederschlags ab. Gegnerischer Schaden vollendet schneller.
		const float Drain = Attributes->GetMaxHealth() * Rules.DownedHealthPct / FMath::Max(1.f, Rules.DownedDuration) * DeltaSeconds;
		const float NewHealth = Attributes->GetHealth() - Drain;
		AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), FMath::Max(0.f, NewHealth));
		if (NewHealth <= 0.f || Now >= DownedEndTime) { Eliminate(nullptr); }
	}
}

void ABotSPlayerState::HandleDamaged(float Damage, AActor* InstigatorActor, bool bStorm)
{
	if (bStorm) { return; } // Sturmschaden beendet den Sturmlauf nicht (Annahme, siehe docs/PROTOTYP.md).
	NotifyCombat();
	if (ABotSHeroCharacter* Hero = GetHero()) { Hero->NotifyDamaged(); }
	const FBotSHeroDef* Def = GetHeroDef();
	if (Def && Def->TraitType == "ShieldOnHit" && IsAlive() && Attributes->GetHealth() > 0.f)
	{
		const float Add = FMath::Min(Damage * Def->Trait("Pct", 0.25f), Def->Trait("Cap", 300.f) - Attributes->GetShield());
		if (Add > 0.f) { AddShield(Add, Def->Trait("Duration", 6.f)); }
	}
}

void ABotSPlayerState::HandleOutOfHealth(AActor* InstigatorActor)
{
	if (LifeState == EBotSLifeState::Alive) { GoDown(InstigatorActor); }
	else if (LifeState == EBotSLifeState::Downed) { Eliminate(InstigatorActor); }
}

void ABotSPlayerState::GoDown(AActor* InstigatorActor)
{
	const FBotSMatchRules& Rules = BotSData().MatchRules;
	LifeState = EBotSLifeState::Downed;
	DownedEndTime = GetWorld()->GetTimeSeconds() + Rules.DownedDuration;
	HotRemaining = 0.f;
	AbilitySystem->AddLooseGameplayTag(TAG_State_Downed);
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetShieldAttribute(), 0.f);
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), Attributes->GetMaxHealth() * Rules.DownedHealthPct);
	if (ABotSHeroCharacter* Hero = GetHero()) { Hero->NotifyDamaged(); }

	// Erfahrung fuer den Trupp des Angreifers, mit Aufholbonus.
	const ABotSHeroCharacter* Killer = Cast<ABotSHeroCharacter>(InstigatorActor);
	const ABotSPlayerState* KillerPS = Killer ? Killer->GetBotSPlayerState() : nullptr;
	if (KillerPS && KillerPS->Squad && KillerPS->Squad != Squad)
	{
		KillerPS->Squad->AddXp(FBotSRules::HeroKillXp(Rules, GetLevel(), KillerPS->Squad->Level));
	}
	UE_LOG(LogBotSMatch, Log, TEXT("Niederschlag: %s (Trupp %d) durch %s"), *GetPlayerName(), SquadIndex, KillerPS ? *KillerPS->GetPlayerName() : TEXT("Umgebung"));
	if (ABotSGameMode* GM = GetWorld()->GetAuthGameMode<ABotSGameMode>()) { GM->OnPlayerDowned(this); }
}

void ABotSPlayerState::Revive()
{
	if (!HasAuthority() || LifeState != EBotSLifeState::Downed) { return; }
	LifeState = EBotSLifeState::Alive;
	AbilitySystem->RemoveLooseGameplayTag(TAG_State_Downed);
	AbilitySystem->SetNumericAttributeBase(UBotSAttributeSet::GetHealthAttribute(), Attributes->GetMaxHealth() * BotSData().MatchRules.ReviveHealthPct);
	UE_LOG(LogBotSMatch, Log, TEXT("Wiederbelebt: %s (Trupp %d)"), *GetPlayerName(), SquadIndex);
}

void ABotSPlayerState::Eliminate(AActor* InstigatorActor)
{
	if (!HasAuthority() || !IsInMatch()) { return; }
	LifeState = EBotSLifeState::Eliminated;
	AbilitySystem->RemoveLooseGameplayTag(TAG_State_Downed);
	SetInStorm(false);
	UE_LOG(LogBotSMatch, Log, TEXT("Ausgeschaltet: %s (Trupp %d)"), *GetPlayerName(), SquadIndex);
	if (ABotSHeroCharacter* Hero = GetHero())
	{
		if (BotSData().MatchRules.bDropItemsOnElimination) { Inventory->DropAll(Hero->GetActorLocation()); }
		if (AController* C = Hero->GetController()) { C->UnPossess(); }
		Hero->Destroy();
	}
	if (ABotSGameMode* GM = GetWorld()->GetAuthGameMode<ABotSGameMode>()) { GM->OnPlayerEliminated(this); }
}

// ---------------------------------------------------------------------------------------------
// SquadState
// ---------------------------------------------------------------------------------------------
ABotSSquadState::ABotSSquadState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
}

void ABotSSquadState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSSquadState, SquadIndex);
	DOREPLIFETIME(ABotSSquadState, Members);
	DOREPLIFETIME(ABotSSquadState, Level);
	DOREPLIFETIME(ABotSSquadState, Xp);
	DOREPLIFETIME(ABotSSquadState, bEliminated);
	DOREPLIFETIME(ABotSSquadState, Placement);
}

void ABotSSquadState::AddXp(float Amount)
{
	if (!HasAuthority() || bEliminated) { return; }
	const FBotSMatchRules& Rules = BotSData().MatchRules;
	if (FBotSRules::AddXp(Level, Xp, Amount, Rules.XpPerLevel, Rules.MaxLevel) > 0)
	{
		UE_LOG(LogBotSMatch, Log, TEXT("Trupp %d erreicht Level %d"), SquadIndex, Level);
		for (ABotSPlayerState* PS : Members) { if (PS) { PS->RecomputeStats(); } }
	}
}

int32 ABotSSquadState::NumAlive() const
{
	int32 Count = 0;
	for (const ABotSPlayerState* PS : Members) { if (PS && PS->IsAlive()) { ++Count; } }
	return Count;
}

int32 ABotSSquadState::NumInMatch() const
{
	int32 Count = 0;
	for (const ABotSPlayerState* PS : Members) { if (PS && PS->IsInMatch()) { ++Count; } }
	return Count;
}

// ---------------------------------------------------------------------------------------------
// GameState
// ---------------------------------------------------------------------------------------------
ABotSGameState::ABotSGameState()
{
}

void ABotSGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSGameState, Phase);
	DOREPLIFETIME(ABotSGameState, PhaseEndTime);
	DOREPLIFETIME(ABotSGameState, Squads);
	DOREPLIFETIME(ABotSGameState, Storm);
	DOREPLIFETIME(ABotSGameState, WinnerSquad);
}

void ABotSGameState::BeginPlay()
{
	Super::BeginPlay();
	// Die Graubox entsteht auf Server und Clients lokal aus denselben Daten und wird nicht repliziert.
	FActorSpawnParameters Params;
	Params.Name = TEXT("BotSMapGeometry");
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ABotSMapGeometry>(ABotSMapGeometry::StaticClass(), FTransform::Identity, Params);
}

void ABotSGameState::GetStormCircle(FVector2D& OutCenter, float& OutRadius) const
{
	if (Storm.Phase <= 0)
	{
		OutCenter = FVector2D::ZeroVector;
		OutRadius = BotSData().Tuning.StormStartDiameter * 0.5f;
		return;
	}
	FBotSRules::StormCircleAt(Storm, GetServerWorldTimeSeconds(), OutCenter, OutRadius);
}

int32 ABotSGameState::NumSquadsAlive() const
{
	int32 Count = 0;
	for (const ABotSSquadState* S : Squads) { if (S && !S->bEliminated && S->Members.Num() > 0) { ++Count; } }
	return Count;
}

int32 ABotSGameState::NumPlayersAlive() const
{
	int32 Count = 0;
	for (const ABotSSquadState* S : Squads) { if (S) { Count += S->NumInMatch(); } }
	return Count;
}

// ---------------------------------------------------------------------------------------------
// GameMode
// ---------------------------------------------------------------------------------------------
ABotSGameMode::ABotSGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	GameStateClass = ABotSGameState::StaticClass();
	PlayerStateClass = ABotSPlayerState::StaticClass();
	PlayerControllerClass = ABotSPlayerController::StaticClass();
	HUDClass = ABotSHUD::StaticClass();
	DefaultPawnClass = nullptr;
}

void ABotSGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const FBotSGameData& Data = BotSData();
	Rng.GenerateNewSeed();
	bAllowBots = Data.MatchRules.bFillWithBots && !UGameplayStatics::HasOption(Options, TEXT("NoBots"));
	// Option BotsOnly: Match ohne Menschen sofort starten (automatischer Testlauf).
	bForceStart = UGameplayStatics::HasOption(Options, TEXT("BotsOnly")) || FParse::Param(FCommandLine::Get(), TEXT("BotsOnly"));
	UE_LOG(LogBotSMatch, Log, TEXT("Match initialisiert: %d Spieler, Truppgroesse %d, Bots %s"), Data.MatchRules.MaxPlayers, Data.MatchRules.SquadSize, bAllowBots ? TEXT("an") : TEXT("aus"));
}

int32 ABotSGameMode::NumHumans() const
{
	int32 Count = 0;
	for (const APlayerState* PS : GameState->PlayerArray) { if (PS && !PS->IsABot() && Cast<APlayerController>(PS->GetOwner())) { ++Count; } }
	return Count;
}

void ABotSGameMode::SetPhase(EBotSMatchPhase NewPhase, float Duration)
{
	ABotSGameState* G = GS();
	G->Phase = NewPhase;
	G->PhaseEndTime = float(G->GetServerWorldTimeSeconds()) + Duration;
	bEarlyFinishArmed = false;
	UE_LOG(LogBotSMatch, Log, TEXT("Phase: %s (%.0f s)"), *UEnum::GetValueAsString(NewPhase), Duration);
}

void ABotSGameMode::AssignSquad(ABotSPlayerState* PS)
{
	ABotSGameState* G = GS();
	const FBotSMatchRules& Rules = BotSData().MatchRules;
	const int32 SquadSize = FMath::Max(1, Rules.SquadSize);
	const int32 NumSquads = FMath::Max(1, Rules.MaxPlayers / SquadSize);
	if (!PS || PS->Squad) { return; }
	for (int32 I = 0; I < NumSquads; ++I)
	{
		if (!G->Squads.IsValidIndex(I))
		{
			ABotSSquadState* NewSquad = GetWorld()->SpawnActor<ABotSSquadState>();
			NewSquad->SquadIndex = I;
			G->Squads.Add(NewSquad);
		}
		ABotSSquadState* Squad = G->Squads[I];
		if (Squad->Members.Num() < SquadSize)
		{
			Squad->Members.Add(PS);
			PS->Squad = Squad;
			PS->SquadIndex = I;
			UE_LOG(LogBotSMatch, Log, TEXT("%s -> Trupp %d"), *PS->GetPlayerName(), I);
			return;
		}
	}
	UE_LOG(LogBotSMatch, Warning, TEXT("Kein freier Platz fuer %s, schaut zu"), *PS->GetPlayerName());
}

void ABotSGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ABotSGameState* G = GS();
	ABotSPlayerState* PS = NewPlayer->GetPlayerState<ABotSPlayerState>();
	if (G->Phase == EBotSMatchPhase::WaitingForPlayers || G->Phase == EBotSMatchPhase::HeroSelect)
	{
		AssignSquad(PS);
		if (G->Phase == EBotSMatchPhase::WaitingForPlayers && G->PhaseEndTime <= 0.f) { SetPhase(EBotSMatchPhase::WaitingForPlayers, BotSData().MatchRules.WaitTime); }
	}
}

void ABotSGameMode::Logout(AController* Exiting)
{
	if (ABotSPlayerState* PS = Exiting ? Exiting->GetPlayerState<ABotSPlayerState>() : nullptr)
	{
		// Getrennte Spieler gelten als ausgeschaltet, kein Wiedereinstieg (TDD Abschnitt 8).
		ABotSSquadState* Squad = PS->Squad;
		PS->Eliminate(nullptr);
		PS->LifeState = EBotSLifeState::Eliminated;
		if (Squad)
		{
			Squad->Members.Remove(PS);
			CheckSquad(Squad);
		}
	}
	Super::Logout(Exiting);
}

void ABotSGameMode::ForceStart()
{
	bForceStart = true;
}

void ABotSGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABotSGameState* G = GS();
	if (!G) { return; }
	const float Now = G->GetServerWorldTimeSeconds();
	const int32 Humans = NumHumans();

	switch (G->Phase)
	{
	case EBotSMatchPhase::WaitingForPlayers:
		if (bForceStart || Humans >= BotSData().MatchRules.MaxPlayers || (Humans > 0 && G->PhaseEndTime > 0.f && Now >= G->PhaseEndTime)) { StartHeroSelect(); }
		break;
	case EBotSMatchPhase::HeroSelect:
	case EBotSMatchPhase::SpawnSelect:
	{
		const bool bHero = G->Phase == EBotSMatchPhase::HeroSelect;
		if (Now >= G->PhaseEndTime)
		{
			if (bHero) { StartSpawnSelect(); } else { StartMatch(); }
			break;
		}
		// Haben alle Menschen gewaehlt, endet die Phase vorzeitig.
		bool bAllChosen = true;
		for (const APlayerState* Base : G->PlayerArray)
		{
			const ABotSPlayerState* PS = Cast<ABotSPlayerState>(Base);
			if (!PS || PS->IsABot() || !PS->Squad) { continue; }
			if (bHero ? PS->HeroId.IsNone() : PS->SpawnChoice < 0) { bAllChosen = false; }
		}
		if (bAllChosen && !bEarlyFinishArmed)
		{
			bEarlyFinishArmed = true;
			G->PhaseEndTime = FMath::Min(G->PhaseEndTime, Now + (Humans > 0 ? 3.f : 0.5f));
		}
		break;
	}
	case EBotSMatchPhase::Ended:
		if (Now >= G->PhaseEndTime && GetWorld()->WorldType == EWorldType::Game)
		{
			// Der Server startet nach dem Siegbildschirm neu. Im Editor bleibt der Siegbildschirm stehen.
			G->PhaseEndTime = TNumericLimits<float>::Max();
			UE_LOG(LogBotSMatch, Log, TEXT("Neustart des Matches"));
			GetWorld()->ServerTravel(TEXT("?Restart"));
		}
		break;
	default:
		break;
	}
}

void ABotSGameMode::StartHeroSelect()
{
	ABotSGameState* G = GS();
	const FBotSGameData& Data = BotSData();
	int32 Total = 0;
	for (const ABotSSquadState* S : G->Squads) { Total += S->Members.Num(); }
	for (int32 I = Total; bAllowBots && I < Data.MatchRules.MaxPlayers; ++I)
	{
		ABotSBotController* Bot = GetWorld()->SpawnActor<ABotSBotController>();
		ABotSPlayerState* PS = Bot ? Bot->GetPlayerState<ABotSPlayerState>() : nullptr;
		if (!PS) { continue; }
		PS->SetPlayerName(FString::Printf(TEXT("Bot %d"), I + 1));
		AssignSquad(PS);
		// Bots waehlen sofort einen freien Helden.
		TArray<FName> Free;
		for (const FBotSHeroDef& H : Data.Heroes) { Free.Add(H.Id); }
		while (Free.Num() > 0)
		{
			const int32 Pick = Rng.RandRange(0, Free.Num() - 1);
			if (SelectHero(PS, Free[Pick])) { break; }
			Free.RemoveAt(Pick);
		}
	}
	SetPhase(EBotSMatchPhase::HeroSelect, Data.MatchRules.HeroSelectTime);
	for (const ABotSSquadState* S : G->Squads)
	{
		for (ABotSPlayerState* PS : S->Members)
		{
			if (!PS || !PS->IsABot() || !PS->HeroId.IsNone()) { continue; }
			for (const FBotSHeroDef& H : Data.Heroes) { if (SelectHero(PS, H.Id)) { break; } }
		}
	}
}

bool ABotSGameMode::SelectHero(ABotSPlayerState* PS, FName HeroId)
{
	ABotSGameState* G = GS();
	const bool bPhaseOk = G->Phase == EBotSMatchPhase::HeroSelect || G->Phase == EBotSMatchPhase::WaitingForPlayers;
	if (!PS || !PS->Squad || !bPhaseOk || !BotSData().FindHero(HeroId)) { return false; }
	// Jeder Held ist pro Trupp nur einmal erlaubt (GDD Abschnitt 3).
	for (const ABotSPlayerState* Other : PS->Squad->Members)
	{
		if (Other && Other != PS && Other->HeroId == HeroId)
		{
			UE_LOG(LogBotSMatch, Verbose, TEXT("Heldenwahl abgelehnt: %s ist im Trupp %d vergeben"), *HeroId.ToString(), PS->SquadIndex);
			return false;
		}
	}
	PS->HeroId = HeroId;
	return true;
}

void ABotSGameMode::SelectSpawn(ABotSPlayerState* PS, int32 SpawnIndex)
{
	if (PS && GS()->Phase == EBotSMatchPhase::SpawnSelect && FBotSMapLayout::Get().Spawns.IsValidIndex(SpawnIndex)) { PS->SpawnChoice = SpawnIndex; }
}

void ABotSGameMode::StartSpawnSelect()
{
	ABotSGameState* G = GS();
	const FBotSGameData& Data = BotSData();
	const int32 NumSpawns = FBotSMapLayout::Get().Spawns.Num();
	for (const ABotSSquadState* S : G->Squads)
	{
		for (ABotSPlayerState* PS : S->Members)
		{
			if (!PS) { continue; }
			// Wer nicht gewaehlt hat, erhaelt einen freien Helden.
			if (PS->HeroId.IsNone())
			{
				G->Phase = EBotSMatchPhase::HeroSelect;
				for (const FBotSHeroDef& H : Data.Heroes) { if (SelectHero(PS, H.Id)) { break; } }
			}
			if (PS->IsABot() && NumSpawns > 0 && PS == S->Members[0]) { PS->SpawnChoice = Rng.RandRange(0, NumSpawns - 1); }
		}
	}
	SetPhase(EBotSMatchPhase::SpawnSelect, Data.MatchRules.SpawnSelectTime);
}

void ABotSGameMode::StartMatch()
{
	ABotSGameState* G = GS();
	const FBotSGameData& Data = BotSData();
	const FBotSMapLayout& Layout = FBotSMapLayout::Get();
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (ABotSSquadState* S : G->Squads)
	{
		// Startpunkt: eigene Wahl, sonst die Wahl des Truppfuehrers, sonst Zufall.
		int32 LeaderChoice = -1;
		for (const ABotSPlayerState* PS : S->Members) { if (PS && PS->SpawnChoice >= 0) { LeaderChoice = PS->SpawnChoice; break; } }
		if (LeaderChoice < 0) { LeaderChoice = Rng.RandRange(0, FMath::Max(0, Layout.Spawns.Num() - 1)); }
		for (int32 I = 0; I < S->Members.Num(); ++I)
		{
			ABotSPlayerState* PS = S->Members[I];
			AController* Controller = PS ? Cast<AController>(PS->GetOwner()) : nullptr;
			if (!Controller) { continue; }
			const int32 Choice = PS->SpawnChoice >= 0 ? PS->SpawnChoice : LeaderChoice;
			const FVector2D Base = Layout.Spawns.IsValidIndex(Choice) ? Layout.Spawns[Choice] : FVector2D::ZeroVector;
			const float Angle = 2.f * PI * I / FMath::Max(1, S->Members.Num());
			const FVector2D Pos = Layout.NearestFree(Base + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 1.2f);
			ABotSHeroCharacter* Hero = World->SpawnActor<ABotSHeroCharacter>(ABotSHeroCharacter::StaticClass(), FTransform(BotSM(Pos, 100.f)), Params);
			if (!Hero) { continue; }
			Controller->Possess(Hero);
			PS->InitForMatch();
			UE_LOG(LogBotSMatch, Log, TEXT("Start: %s als %s, Trupp %d, Punkt %d"), *PS->GetPlayerName(), *PS->HeroId.ToString(), S->SquadIndex, Choice);
		}
	}

	for (const FVector2D& Chest : Layout.Chests) { World->SpawnActor<ABotSLootChest>(ABotSLootChest::StaticClass(), FTransform(BotSM(Chest, 50.f)), Params); }
	for (const FVector2D& Camp : Layout.Camps)
	{
		for (int32 I = 0; I < Data.Creature.CampSize; ++I)
		{
			const float Angle = 2.f * PI * I / FMath::Max(1, Data.Creature.CampSize);
			World->SpawnActor<ABotSCreature>(ABotSCreature::StaticClass(), FTransform(BotSM(Camp + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 2.5f, 100.f)), Params);
		}
	}

	StormController = World->SpawnActor<ABotSStormController>();
	StormController->StartStorm();
	SetPhase(EBotSMatchPhase::Match, 0.f);
	UE_LOG(LogBotSMatch, Log, TEXT("Match gestartet: %d Trupps, %d Truhen, %d Lager"), G->Squads.Num(), Layout.Chests.Num(), Layout.Camps.Num());
	CheckWin();
}

void ABotSGameMode::OnPlayerDowned(ABotSPlayerState* PS) { if (PS) { CheckSquad(PS->Squad); } }
void ABotSGameMode::OnPlayerEliminated(ABotSPlayerState* PS) { if (PS) { CheckSquad(PS->Squad); } }

void ABotSGameMode::CheckSquad(ABotSSquadState* Squad)
{
	ABotSGameState* G = GS();
	if (!Squad || Squad->bEliminated || G->Phase != EBotSMatchPhase::Match || Squad->NumAlive() > 0) { return; }
	// Alle Mitglieder gleichzeitig niedergeschlagen oder ausgeschaltet: Trupp ausgeschieden (GDD Abschnitt 10).
	Squad->bEliminated = true;
	Squad->Placement = G->NumSquadsAlive() + 1;
	LastEliminatedSquad = Squad->SquadIndex;
	UE_LOG(LogBotSMatch, Log, TEXT("Trupp %d ausgeschieden, Platz %d"), Squad->SquadIndex, Squad->Placement);
	const TArray<TObjectPtr<ABotSPlayerState>> Members = Squad->Members;
	for (ABotSPlayerState* PS : Members) { if (PS) { PS->Eliminate(nullptr); } }
	CheckWin();
}

void ABotSGameMode::CheckWin()
{
	ABotSGameState* G = GS();
	if (G->Phase != EBotSMatchPhase::Match) { return; }
	int32 Alive = 0, Last = -1;
	for (const ABotSSquadState* S : G->Squads) { if (S && !S->bEliminated && S->Members.Num() > 0) { ++Alive; Last = S->SquadIndex; } }
	if (Alive <= 1) { EndMatch(Alive == 1 ? Last : LastEliminatedSquad); }
}

void ABotSGameMode::EndMatch(int32 WinnerSquad)
{
	ABotSGameState* G = GS();
	G->WinnerSquad = WinnerSquad;
	if (G->Squads.IsValidIndex(WinnerSquad)) { G->Squads[WinnerSquad]->Placement = 1; }
	UE_LOG(LogBotSMatch, Log, TEXT("Match beendet: Sieger Trupp %d"), WinnerSquad);
	SetPhase(EBotSMatchPhase::Ended, BotSData().MatchRules.EndScreenTime);
}
