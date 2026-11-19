#include "Abilities/BotSAbilities.h"
#include "BattleOfTheStorm.h"
#include "Character/BotSCharacters.h"
#include "Match/BotSMatch.h"
#include "Items/BotSItems.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// ---------------------------------------------------------------------------------------------
// Attribute
// ---------------------------------------------------------------------------------------------
void UBotSAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, HealthRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, ManaRegen, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, Shield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, AttackDamage, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, AttackSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, AttackRange, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, MoveSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UBotSAttributeSet, CooldownReduction, COND_None, REPNOTIFY_Always);
}

#define BOTS_ONREP(Name) void UBotSAttributeSet::OnRep_##Name(const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(UBotSAttributeSet, Name, Old); }
BOTS_ONREP(Health) BOTS_ONREP(MaxHealth) BOTS_ONREP(HealthRegen) BOTS_ONREP(Mana) BOTS_ONREP(MaxMana) BOTS_ONREP(ManaRegen)
BOTS_ONREP(Shield) BOTS_ONREP(AttackDamage) BOTS_ONREP(AttackSpeed) BOTS_ONREP(AttackRange) BOTS_ONREP(MoveSpeed) BOTS_ONREP(CooldownReduction)
#undef BOTS_ONREP

void UBotSAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (Attribute == GetHealthAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth()); }
	else if (Attribute == GetManaAttribute()) { NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana()); }
	else if (Attribute == GetShieldAttribute()) { NewValue = FMath::Max(0.f, NewValue); }
}

void UBotSAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	AActor* InstigatorActor = Data.EffectSpec.GetContext().GetInstigator();

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float Damage = GetIncomingDamage();
		SetIncomingDamage(0.f);
		if (Damage <= 0.f || GetHealth() <= 0.f) { return; }
		const bool bStorm = Data.EffectSpec.GetDynamicAssetTags().HasTagExact(TAG_Damage_Storm);
		if (!bStorm && ASC && ASC->HasMatchingGameplayTag(TAG_State_Invulnerable)) { return; }

		// Reihenfolge laut TDD: IncomingDamage, dann Shield, dann Health.
		float NewShield = GetShield(), NewHealth = GetHealth();
		FBotSRules::SplitDamage(Damage, bStorm && !BotSData().Tuning.bShieldBlocksStorm, NewShield, NewHealth);
		SetShield(NewShield);
		SetHealth(NewHealth);
		OnDamaged.Broadcast(Damage, InstigatorActor, bStorm);
		if (NewHealth <= 0.f) { OnOutOfHealth.Broadcast(InstigatorActor); }
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		float Heal = GetIncomingHealing();
		SetIncomingHealing(0.f);
		if (Heal <= 0.f || GetHealth() <= 0.f) { return; }
		// Wirkung im Sturm: Heilung um 50 % reduziert (GDD Abschnitt 4).
		if (ASC && ASC->HasMatchingGameplayTag(TAG_State_InStorm)) { Heal *= 0.5f; }
		SetHealth(FMath::Min(GetMaxHealth(), GetHealth() + Heal));
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.f, GetMaxMana()));
	}
}

// ---------------------------------------------------------------------------------------------
// Effekte
// ---------------------------------------------------------------------------------------------
static FGameplayEffectModifierMagnitude BotSSetByCaller(const FGameplayTag& Tag)
{
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = Tag;
	return FGameplayEffectModifierMagnitude(SetByCaller);
}

static void BotSAddModifier(UGameplayEffect* Effect, const FGameplayAttribute& Attribute, EGameplayModOp::Type Op, const FGameplayTag& Tag)
{
	FGameplayModifierInfo Mod;
	Mod.Attribute = Attribute;
	Mod.ModifierOp = Op;
	Mod.ModifierMagnitude = BotSSetByCaller(Tag);
	Effect->Modifiers.Add(Mod);
}

UBotSGE_Damage::UBotSGE_Damage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	BotSAddModifier(this, UBotSAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::AddBase, TAG_Data_Damage);
}

UBotSGE_Heal::UBotSGE_Heal()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	BotSAddModifier(this, UBotSAttributeSet::GetIncomingHealingAttribute(), EGameplayModOp::AddBase, TAG_Data_Heal);
}

UBotSGE_Cost::UBotSGE_Cost()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	BotSAddModifier(this, UBotSAttributeSet::GetManaAttribute(), EGameplayModOp::AddBase, TAG_Data_Mana);
}

UBotSGE_Cooldown::UBotSGE_Cooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = BotSSetByCaller(TAG_Data_Duration);
}

UBotSGE_TimedState::UBotSGE_TimedState()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = BotSSetByCaller(TAG_Data_Duration);
	BotSAddModifier(this, UBotSAttributeSet::GetMoveSpeedAttribute(), EGameplayModOp::MultiplyAdditive, TAG_Data_MoveSpeedMul);
	BotSAddModifier(this, UBotSAttributeSet::GetAttackSpeedAttribute(), EGameplayModOp::MultiplyAdditive, TAG_Data_AttackSpeedMul);
}

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Basic, "Cooldown.Basic");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_1, "Cooldown.1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_2, "Cooldown.2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_3, "Cooldown.3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_4, "Cooldown.4");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_5, "Cooldown.5");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_StormRun, "Cooldown.StormRun");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Item1, "Cooldown.Item1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Cooldown_Item2, "Cooldown.Item2");

FGameplayTag BotSCooldownTag(EBotSSlot Slot)
{
	switch (Slot)
	{
	case EBotSSlot::Basic: return TAG_Cooldown_Basic;
	case EBotSSlot::Ability1: return TAG_Cooldown_1;
	case EBotSSlot::Ability2: return TAG_Cooldown_2;
	case EBotSSlot::Ability3: return TAG_Cooldown_3;
	case EBotSSlot::Ultimate: return TAG_Cooldown_4;
	case EBotSSlot::Trait: return TAG_Cooldown_5;
	case EBotSSlot::StormRun: return TAG_Cooldown_StormRun;
	case EBotSSlot::Item1: return TAG_Cooldown_Item1;
	case EBotSSlot::Item2: return TAG_Cooldown_Item2;
	default: return FGameplayTag();
	}
}

// ---------------------------------------------------------------------------------------------
// Kampf-Hilfsfunktionen
// ---------------------------------------------------------------------------------------------
namespace BotSCombat
{
	static FGameplayEffectSpecHandle MakeSpec(UAbilitySystemComponent* TargetASC, TSubclassOf<UGameplayEffect> Class, AActor* Source)
	{
		FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
		if (Source) { Context.AddInstigator(Source, Source); }
		return TargetASC->MakeOutgoingSpec(Class, 1.f, Context);
	}

	void ApplyDamage(AActor* Source, ABotSCombatCharacter* Target, float Damage, bool bStorm)
	{
		UAbilitySystemComponent* ASC = Target ? Target->GetAbilitySystemComponent() : nullptr;
		if (!ASC || Damage <= 0.f) { return; }
		FGameplayEffectSpecHandle Spec = MakeSpec(ASC, UBotSGE_Damage::StaticClass(), Source);
		if (!Spec.IsValid()) { return; }
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, Damage);
		if (bStorm) { Spec.Data->AddDynamicAssetTag(TAG_Damage_Storm); }
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	void ApplyHeal(AActor* Source, ABotSCombatCharacter* Target, float Heal)
	{
		UAbilitySystemComponent* ASC = Target ? Target->GetAbilitySystemComponent() : nullptr;
		if (!ASC || Heal <= 0.f) { return; }
		FGameplayEffectSpecHandle Spec = MakeSpec(ASC, UBotSGE_Heal::StaticClass(), Source);
		if (!Spec.IsValid()) { return; }
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Heal, Heal);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	void ApplyTimedState(ABotSCombatCharacter* Target, float Duration, float MoveSpeedMul, float AttackSpeedMul, FGameplayTag StateTag)
	{
		UAbilitySystemComponent* ASC = Target ? Target->GetAbilitySystemComponent() : nullptr;
		if (!ASC || Duration <= 0.f) { return; }
		FGameplayEffectSpecHandle Spec = MakeSpec(ASC, UBotSGE_TimedState::StaticClass(), nullptr);
		if (!Spec.IsValid()) { return; }
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Duration, Duration);
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_MoveSpeedMul, MoveSpeedMul);
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_AttackSpeedMul, AttackSpeedMul);
		if (StateTag.IsValid()) { Spec.Data->DynamicGrantedTags.AddTag(StateTag); }
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}

	void ApplyPayload(ABotSCombatCharacter* Source, int32 SourceTeam, ABotSCombatCharacter* Target, const TMap<FName, float>& P, const FVector& Origin)
	{
		if (!Target || !Target->IsTargetable()) { return; }
		auto Get = [&P](FName Key) { const float* V = P.Find(Key); return V ? *V : 0.f; };
		const bool bHostile = Target->GetTeam() != SourceTeam;

		if (bHostile)
		{
			if (Get("Damage") > 0.f) { ApplyDamage(Source, Target, Get("Damage")); }
			if (!Target->IsTargetable()) { return; }
			if (Get("SlowPct") > 0.f) { ApplyTimedState(Target, Get("SlowDuration"), 1.f - FMath::Clamp(Get("SlowPct"), 0.f, 0.9f), 1.f, FGameplayTag()); }
			if (Get("Stun") > 0.f) { ApplyTimedState(Target, Get("Stun"), 1.f, 1.f, TAG_State_Stunned); }
			if (Get("Root") > 0.f) { ApplyTimedState(Target, Get("Root"), 1.f, 1.f, TAG_State_Rooted); }
			if (Get("Knockback") > 0.f)
			{
				FVector Dir = (Target->GetActorLocation() - Origin).GetSafeNormal2D();
				if (Dir.IsNearlyZero() && Source) { Dir = Source->GetActorForwardVector(); }
				Target->Shove(Dir, Get("Knockback"));
			}
			return;
		}

		if (Target->IsDowned()) { return; }
		ABotSHeroCharacter* TargetHero = Cast<ABotSHeroCharacter>(Target);
		ABotSPlayerState* TargetPS = TargetHero ? TargetHero->GetBotSPlayerState() : nullptr;
		if (Get("Heal") > 0.f)
		{
			float Heal = Get("Heal");
			// Merkmal des Heilers: staerkere Heilung auf schwache Ziele.
			const ABotSHeroCharacter* SourceHero = Cast<ABotSHeroCharacter>(Source);
			const ABotSPlayerState* SourcePS = SourceHero ? SourceHero->GetBotSPlayerState() : nullptr;
			const FBotSHeroDef* Def = SourcePS ? SourcePS->GetHeroDef() : nullptr;
			if (Def && Def->TraitType == "HealBonusLowHealth" && Target->GetHealth() < Target->GetMaxHealth() * Def->Trait("Threshold", 0.3f))
			{
				Heal *= 1.f + Def->Trait("Bonus", 0.25f);
			}
			ApplyHeal(Source, Target, Heal);
		}
		if (TargetPS)
		{
			if (Get("HealOverTime") > 0.f) { TargetPS->AddHealOverTime(Get("HealOverTime"), FMath::Max(0.5f, Get("HealDuration"))); }
			if (Get("Shield") > 0.f) { TargetPS->AddShield(Get("Shield"), FMath::Max(0.5f, Get("ShieldDuration"))); }
		}
		if (Get("HastePct") > 0.f) { ApplyTimedState(Target, Get("HasteDuration"), 1.f + Get("HastePct"), 1.f, FGameplayTag()); }
	}

	void GetTargetsInRadius(UWorld* World, const FVector& Center, float RadiusM, TArray<ABotSCombatCharacter*>& Out)
	{
		Out.Reset();
		const float Reach = BotSM(RadiusM) + 45.f; // Koerperradius des Ziels
		for (TActorIterator<ABotSCombatCharacter> It(World); It; ++It)
		{
			if (It->IsTargetable() && FVector::DistSquared2D(It->GetActorLocation(), Center) <= Reach * Reach) { Out.Add(*It); }
		}
	}

	int32 GetLocalTeam(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const ABotSPlayerState* PS = (PC && PC->IsLocalController()) ? PC->GetPlayerState<ABotSPlayerState>() : nullptr;
		return PS ? PS->SquadIndex : -2;
	}

	FLinearColor RelationColor(UWorld* World, int32 Team)
	{
		if (Team < 0) { return FLinearColor(1.f, 0.8f, 0.1f); }
		return Team == GetLocalTeam(World) ? FLinearColor(0.15f, 0.45f, 1.f) : FLinearColor(1.f, 0.15f, 0.1f);
	}

	UStaticMesh* Shape(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
	}

	void SetMeshColor(UStaticMeshComponent* Mesh, const FLinearColor& Color)
	{
		if (!Mesh || Mesh->GetNetMode() == NM_DedicatedServer) { return; }
		static const TCHAR* MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!MID)
		{
			UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
			if (!Base) { return; }
			MID = UMaterialInstanceDynamic::Create(Base, Mesh);
			Mesh->SetMaterial(0, MID);
		}
		MID->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

// ---------------------------------------------------------------------------------------------
// Faehigkeit
// ---------------------------------------------------------------------------------------------
UBotSGameplayAbility::UBotSGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// Abweichung vom TDD: Start wird nicht lokal vorhergesagt. Die Wirkung entsteht ohnehin nur auf dem Server.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

static bool BotSResolve(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, EBotSSlot& OutSlot, ABotSPlayerState*& OutPS, ABotSHeroCharacter*& OutHero, FBotSAbilityDef& OutDef)
{
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Spec) { return false; }
	OutSlot = EBotSSlot(Spec->InputID);
	OutPS = Cast<ABotSPlayerState>(ActorInfo->OwnerActor.Get());
	OutHero = Cast<ABotSHeroCharacter>(ActorInfo->AvatarActor.Get());
	return OutPS && OutHero && OutPS->GetAbilityDef(OutSlot, OutDef);
}

static float BotSCooldownFor(EBotSSlot Slot, const FBotSAbilityDef& Def, const UBotSAttributeSet* Attr)
{
	if (Slot == EBotSSlot::Basic) { return 1.f / FMath::Max(0.1f, Attr->GetAttackSpeed()); }
	if (Slot == EBotSSlot::Item1 || Slot == EBotSSlot::Item2) { return Def.Cooldown; }
	return Def.Cooldown * (1.f - FMath::Clamp(Attr->GetCooldownReduction(), 0.f, 0.4f));
}

bool UBotSGameplayAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags)) { return false; }
	EBotSSlot Slot; ABotSPlayerState* PS; ABotSHeroCharacter* Hero; FBotSAbilityDef Def;
	if (!BotSResolve(Handle, ActorInfo, Slot, PS, Hero, Def)) { return false; }
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	if (!PS->IsAlive() || ASC->HasMatchingGameplayTag(TAG_State_Stunned)) { return false; }
	if (ASC->HasMatchingGameplayTag(BotSCooldownTag(Slot))) { return false; }
	if (Slot == EBotSSlot::Ultimate && PS->GetLevel() < BotSData().MatchRules.UltimateLevel) { return false; }
	if (Def.Type == EBotSAbilityType::Dash && ASC->HasMatchingGameplayTag(TAG_State_Rooted)) { return false; }
	if (PS->GetAttributes()->GetMana() < Def.ManaCost) { return false; }
	return true;
}

void UBotSGameplayAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	EBotSSlot Slot; ABotSPlayerState* PS; ABotSHeroCharacter* Hero; FBotSAbilityDef Def;
	if (!BotSResolve(Handle, ActorInfo, Slot, PS, Hero, Def) || !Hero->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const UBotSAttributeSet* Attr = PS->GetAttributes();
	UWorld* World = Hero->GetWorld();

	// Aktivgegenstand: Ladung verbrauchen.
	if (Slot == EBotSSlot::Item1 || Slot == EBotSSlot::Item2)
	{
		if (!PS->GetInventory()->ConsumeActive(Slot == EBotSSlot::Item1 ? 0 : 1))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	// Abklingzeit und Kosten.
	{
		FGameplayEffectSpecHandle Cooldown = ASC->MakeOutgoingSpec(UBotSGE_Cooldown::StaticClass(), 1.f, ASC->MakeEffectContext());
		Cooldown.Data->SetSetByCallerMagnitude(TAG_Data_Duration, FMath::Max(0.05f, BotSCooldownFor(Slot, Def, Attr)));
		Cooldown.Data->DynamicGrantedTags.AddTag(BotSCooldownTag(Slot));
		ASC->ApplyGameplayEffectSpecToSelf(*Cooldown.Data);
		if (Def.ManaCost > 0.f)
		{
			FGameplayEffectSpecHandle Cost = ASC->MakeOutgoingSpec(UBotSGE_Cost::StaticClass(), 1.f, ASC->MakeEffectContext());
			Cost.Data->SetSetByCallerMagnitude(TAG_Data_Mana, -Def.ManaCost);
			ASC->ApplyGameplayEffectSpecToSelf(*Cost.Data);
		}
	}

	const float BasicFactor = Hero->NotifyAbilityUsed(Slot);
	const float LevelFactor = PS->GetLevelFactor();
	const int32 Team = Hero->GetTeam();
	const FVector Origin = Hero->GetActorLocation();
	FVector ToAim = Hero->GetAim() - Origin;
	ToAim.Z = 0.f;
	const float AimDist = ToAim.Size();
	const FVector Dir = AimDist > 1.f ? ToAim / AimDist : Hero->GetActorForwardVector().GetSafeNormal2D();

	// Endwerte vorab berechnen: Faehigkeitswerte skalieren mit dem Trupp-Level, der Basisangriff mit AttackDamage.
	TMap<FName, float> P = Def.Params;
	const float Damage = Def.P("Damage") * LevelFactor + Def.P("ApMul") * Attr->GetAttackDamage() * BasicFactor;
	if (Damage > 0.f) { P.Add("Damage", Damage); }
	for (FName Key : { FName("Heal"), FName("HealOverTime"), FName("Shield") })
	{
		if (float* V = P.Find(Key)) { *V *= LevelFactor; }
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto SpawnArea = [&](const FVector& Center, float Radius, float Delay)
	{
		const FVector Ground(Center.X, Center.Y, Origin.Z - Hero->GetSimpleCollisionHalfHeight() + 4.f);
		ABotSAreaEffect* Area = World->SpawnActorDeferred<ABotSAreaEffect>(ABotSAreaEffect::StaticClass(), FTransform(Ground), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Area->Radius = Radius;
		Area->Delay = Delay;
		Area->Duration = Def.P("Duration");
		Area->Interval = FMath::Max(0.1f, Def.P("Interval", 0.5f));
		Area->bFollow = Def.P("Follow") > 0.f;
		Area->Team = Team;
		Area->Source = Hero;
		Area->Params = P;
		Area->FinishSpawning(FTransform(Ground));
	};

	switch (Def.Type)
	{
	case EBotSAbilityType::Melee:
	{
		const float Range = Def.P("Range") > 0.f ? Def.P("Range") : Attr->GetAttackRange();
		const float CosHalf = FMath::Cos(FMath::DegreesToRadians(Def.P("Angle", 120.f) * 0.5f));
		TArray<ABotSCombatCharacter*> Targets;
		BotSCombat::GetTargetsInRadius(World, Origin, Range, Targets);
		for (ABotSCombatCharacter* T : Targets)
		{
			if (T == Hero) { continue; }
			const FVector To = (T->GetActorLocation() - Origin).GetSafeNormal2D();
			if (FVector::DotProduct(To, Dir) >= CosHalf) { BotSCombat::ApplyPayload(Hero, Team, T, P, Origin); }
		}
		break;
	}
	case EBotSAbilityType::Projectile:
	{
		const FVector Start = Origin + Dir * 60.f;
		ABotSProjectile* Proj = World->SpawnActorDeferred<ABotSProjectile>(ABotSProjectile::StaticClass(), FTransform(Start), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Proj->Start = Start;
		Proj->Dir = Dir;
		Proj->Speed = FMath::Max(1.f, Def.P("Speed", 20.f));
		Proj->Range = Def.P("Range") > 0.f ? Def.P("Range") : Attr->GetAttackRange();
		Proj->Radius = Def.P("Radius", 0.4f);
		Proj->Team = Team;
		Proj->Source = Hero;
		Proj->Params = P;
		Proj->FinishSpawning(FTransform(Start));
		break;
	}
	case EBotSAbilityType::Area:
	{
		const float Range = Def.P("Range");
		const FVector Center = Range > 0.f ? Origin + Dir * FMath::Min(AimDist, BotSM(Range)) : Origin;
		SpawnArea(Center, Def.P("Radius", 3.f), Def.P("Delay"));
		BotSCombat::ApplyTimedState(Hero, Def.P("Invulnerable"), 1.f, 1.f, TAG_State_Invulnerable);
		break;
	}
	case EBotSAbilityType::Dash:
	{
		// Der Sprung ist ein sofortiger, an Waenden endender Versatz. Der Client wird ueber die Bewegungskorrektur nachgezogen.
		Hero->Shove(Dir, FMath::Min(Def.P("Distance", 5.f), FMath::Max(1.f, AimDist / 100.f)));
		BotSCombat::ApplyPayload(Hero, Team, Hero, P, Origin);
		if (Def.P("EndRadius") > 0.f) { SpawnArea(Hero->GetActorLocation(), Def.P("EndRadius"), 0.05f); }
		break;
	}
	case EBotSAbilityType::SelfBuff:
		BotSCombat::ApplyPayload(Hero, Team, Hero, P, Origin);
		BotSCombat::ApplyTimedState(Hero, Def.P("Invulnerable"), 1.f, 1.f, TAG_State_Invulnerable);
		break;
	}

	UE_LOG(LogBotSAbility, Verbose, TEXT("%s nutzt %s"), *PS->GetPlayerName(), *Def.Id.ToString());
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

// ---------------------------------------------------------------------------------------------
// Projektil
// ---------------------------------------------------------------------------------------------
ABotSProjectile::ABotSProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>("Mesh");
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;
}

void ABotSProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSProjectile, Start);
	DOREPLIFETIME(ABotSProjectile, Dir);
	DOREPLIFETIME(ABotSProjectile, Speed);
	DOREPLIFETIME(ABotSProjectile, Range);
	DOREPLIFETIME(ABotSProjectile, Radius);
	DOREPLIFETIME(ABotSProjectile, Team);
}

void ABotSProjectile::BeginPlay()
{
	Super::BeginPlay();
	Mesh->SetStaticMesh(BotSCombat::Shape(TEXT("Sphere")));
	Mesh->SetWorldScale3D(FVector(FMath::Max(0.2f, Radius * 2.f)));
	BotSCombat::SetMeshColor(Mesh, BotSCombat::RelationColor(GetWorld(), Team));
}

void ABotSProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FVector Prev = Start + FVector(Dir) * Travelled;
	Travelled += BotSM(Speed) * DeltaSeconds;
	const bool bDone = Travelled >= BotSM(Range);
	const FVector Cur = Start + FVector(Dir) * FMath::Min(Travelled, BotSM(Range));
	SetActorLocation(Cur);
	if (!HasAuthority()) { return; }

	const float Reach = BotSM(Radius) + 45.f;
	const bool bPierce = Params.FindRef("Pierce") > 0.f;
	for (TActorIterator<ABotSCombatCharacter> It(GetWorld()); It; ++It)
	{
		ABotSCombatCharacter* T = *It;
		if (!T->IsTargetable() || T->GetTeam() == Team || HitActors.Contains(T)) { continue; }
		const FVector Closest = FMath::ClosestPointOnSegment(T->GetActorLocation(), Prev, Cur);
		if (FVector::DistSquared2D(Closest, T->GetActorLocation()) > Reach * Reach) { continue; }
		HitActors.Add(T);
		BotSCombat::ApplyPayload(Source.Get(), Team, T, Params, Prev);
		if (!bPierce) { Destroy(); return; }
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(BotSProjectile), false);
	if (bDone || GetWorld()->LineTraceTestByChannel(Prev, Cur, ECC_Visibility, Query)) { Destroy(); }
}

// ---------------------------------------------------------------------------------------------
// Flaeche
// ---------------------------------------------------------------------------------------------
ABotSAreaEffect::ABotSAreaEffect()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicatingMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>("Mesh");
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	RootComponent = Mesh;
}

void ABotSAreaEffect::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSAreaEffect, Radius);
	DOREPLIFETIME(ABotSAreaEffect, Delay);
	DOREPLIFETIME(ABotSAreaEffect, Duration);
	DOREPLIFETIME(ABotSAreaEffect, Team);
}

void ABotSAreaEffect::BeginPlay()
{
	Super::BeginPlay();
	Mesh->SetStaticMesh(BotSCombat::Shape(TEXT("Cylinder")));
	Mesh->SetWorldScale3D(FVector(Radius * 2.f, Radius * 2.f, 0.04f));
	BotSCombat::SetMeshColor(Mesh, BotSCombat::RelationColor(GetWorld(), Team) * 0.3f);
	NextPulse = Delay;
}

void ABotSAreaEffect::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	if (!bArmedVisual && Age >= Delay)
	{
		// Nach der Vorwarnzeit wechselt die Flaeche auf volle Farbe.
		bArmedVisual = true;
		BotSCombat::SetMeshColor(Mesh, BotSCombat::RelationColor(GetWorld(), Team));
	}
	if (!HasAuthority()) { return; }
	if (bFollow && Source.IsValid())
	{
		const FVector L = Source->GetActorLocation();
		SetActorLocation(FVector(L.X, L.Y, GetActorLocation().Z));
	}
	while (Age >= NextPulse)
	{
		Pulse();
		NextPulse += Interval;
		if (Duration <= 0.f) { NextPulse = TNumericLimits<float>::Max(); }
	}
	const float Life = Duration > 0.f ? Delay + Duration : Delay + 0.35f;
	if (Age >= Life) { Destroy(); }
}

void ABotSAreaEffect::Pulse()
{
	TArray<ABotSCombatCharacter*> Targets;
	BotSCombat::GetTargetsInRadius(GetWorld(), GetActorLocation(), Radius, Targets);
	for (ABotSCombatCharacter* T : Targets) { BotSCombat::ApplyPayload(Source.Get(), Team, T, Params, GetActorLocation()); }
}
