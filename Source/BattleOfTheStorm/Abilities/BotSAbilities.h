#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "GameplayEffect.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Actor.h"
#include "Core/BotSTypes.h"
#include "BotSAbilities.generated.h"

class ABotSCombatCharacter;
class UStaticMeshComponent;

#define BOTS_ATTRIBUTE(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class UBotSAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(FBotSOnDamaged, float /*Damage*/, AActor* /*Instigator*/, bool /*bStorm*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FBotSOnOutOfHealth, AActor* /*Instigator*/);

/** Attribute laut TDD Abschnitt 6. */
UCLASS()
class UBotSAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	UPROPERTY(ReplicatedUsing = OnRep_Health) FGameplayAttributeData Health;
	BOTS_ATTRIBUTE(UBotSAttributeSet, Health)
	UPROPERTY(ReplicatedUsing = OnRep_MaxHealth) FGameplayAttributeData MaxHealth;
	BOTS_ATTRIBUTE(UBotSAttributeSet, MaxHealth)
	UPROPERTY(ReplicatedUsing = OnRep_HealthRegen) FGameplayAttributeData HealthRegen;
	BOTS_ATTRIBUTE(UBotSAttributeSet, HealthRegen)
	UPROPERTY(ReplicatedUsing = OnRep_Mana) FGameplayAttributeData Mana;
	BOTS_ATTRIBUTE(UBotSAttributeSet, Mana)
	UPROPERTY(ReplicatedUsing = OnRep_MaxMana) FGameplayAttributeData MaxMana;
	BOTS_ATTRIBUTE(UBotSAttributeSet, MaxMana)
	UPROPERTY(ReplicatedUsing = OnRep_ManaRegen) FGameplayAttributeData ManaRegen;
	BOTS_ATTRIBUTE(UBotSAttributeSet, ManaRegen)
	UPROPERTY(ReplicatedUsing = OnRep_Shield) FGameplayAttributeData Shield;
	BOTS_ATTRIBUTE(UBotSAttributeSet, Shield)
	UPROPERTY(ReplicatedUsing = OnRep_AttackDamage) FGameplayAttributeData AttackDamage;
	BOTS_ATTRIBUTE(UBotSAttributeSet, AttackDamage)
	UPROPERTY(ReplicatedUsing = OnRep_AttackSpeed) FGameplayAttributeData AttackSpeed;
	BOTS_ATTRIBUTE(UBotSAttributeSet, AttackSpeed)
	UPROPERTY(ReplicatedUsing = OnRep_AttackRange) FGameplayAttributeData AttackRange;
	BOTS_ATTRIBUTE(UBotSAttributeSet, AttackRange)
	UPROPERTY(ReplicatedUsing = OnRep_MoveSpeed) FGameplayAttributeData MoveSpeed;
	BOTS_ATTRIBUTE(UBotSAttributeSet, MoveSpeed)
	UPROPERTY(ReplicatedUsing = OnRep_CooldownReduction) FGameplayAttributeData CooldownReduction;
	BOTS_ATTRIBUTE(UBotSAttributeSet, CooldownReduction)

	/** Rechenwerte, nicht repliziert. */
	UPROPERTY() FGameplayAttributeData IncomingDamage;
	BOTS_ATTRIBUTE(UBotSAttributeSet, IncomingDamage)
	UPROPERTY() FGameplayAttributeData IncomingHealing;
	BOTS_ATTRIBUTE(UBotSAttributeSet, IncomingHealing)

	FBotSOnDamaged OnDamaged;
	FBotSOnOutOfHealth OnOutOfHealth;

protected:
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_HealthRegen(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Mana(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxMana(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_ManaRegen(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_Shield(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_AttackDamage(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_AttackSpeed(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_AttackRange(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MoveSpeed(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_CooldownReduction(const FGameplayAttributeData& Old);
};

// ---------------------------------------------------------------------------------------------
// Effekte. Zahlenwerte kommen ueber SetByCaller aus den Daten, nie aus dem Code.
// ---------------------------------------------------------------------------------------------
UCLASS() class UBotSGE_Damage : public UGameplayEffect { GENERATED_BODY() public: UBotSGE_Damage(); };
UCLASS() class UBotSGE_Heal : public UGameplayEffect { GENERATED_BODY() public: UBotSGE_Heal(); };
UCLASS() class UBotSGE_Cost : public UGameplayEffect { GENERATED_BODY() public: UBotSGE_Cost(); };
/** Abklingzeit. Das Tag des Slots wird je Anwendung als DynamicGrantedTag gesetzt. */
UCLASS() class UBotSGE_Cooldown : public UGameplayEffect { GENERATED_BODY() public: UBotSGE_Cooldown(); };
/** Zeitlich begrenzter Zustand: Tempo, Angriffstempo und ein Zustands-Tag (Betaeubung, Festhalten, Unverwundbar). */
UCLASS() class UBotSGE_TimedState : public UGameplayEffect { GENERATED_BODY() public: UBotSGE_TimedState(); };

BATTLEOFTHESTORM_API FGameplayTag BotSCooldownTag(EBotSSlot Slot);

// ---------------------------------------------------------------------------------------------
// Kampf-Hilfsfunktionen, nur auf dem Server
// ---------------------------------------------------------------------------------------------
namespace BotSCombat
{
	void ApplyDamage(AActor* Source, ABotSCombatCharacter* Target, float Damage, bool bStorm = false);
	void ApplyHeal(AActor* Source, ABotSCombatCharacter* Target, float Heal);
	void ApplyTimedState(ABotSCombatCharacter* Target, float Duration, float MoveSpeedMul, float AttackSpeedMul, FGameplayTag StateTag);
	/** Wendet die Werte einer Faehigkeit an: Gegner erhalten Schaden und Kontrolle, Verbuendete Heilung, Schild und Tempo. */
	void ApplyPayload(ABotSCombatCharacter* Source, int32 SourceTeam, ABotSCombatCharacter* Target, const TMap<FName, float>& P, const FVector& Origin);
	void GetTargetsInRadius(UWorld* World, const FVector& Center, float RadiusM, TArray<ABotSCombatCharacter*>& Out);
	int32 GetLocalTeam(UWorld* World);
	/** Farbe nach Beziehung zum Betrachter: verbuendet blau, gegnerisch rot, neutral gelb (GDD Abschnitt 11). */
	FLinearColor RelationColor(UWorld* World, int32 Team);
	UStaticMesh* Shape(const TCHAR* Name);
	void SetMeshColor(UStaticMeshComponent* Mesh, const FLinearColor& Color);
}

/** Datengetriebene Faehigkeit. Slot und Werte kommen aus dem Helden-Datensatz, die Bausteine stehen in EBotSAbilityType. */
UCLASS()
class UBotSGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UBotSGameplayAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};

UCLASS()
class ABotSProjectile : public AActor
{
	GENERATED_BODY()

public:
	ABotSProjectile();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated) FVector_NetQuantize Start;
	UPROPERTY(Replicated) FVector_NetQuantizeNormal Dir;
	/** Meter pro Sekunde, Meter. */
	UPROPERTY(Replicated) float Speed = 20.f;
	UPROPERTY(Replicated) float Range = 10.f;
	UPROPERTY(Replicated) float Radius = 0.4f;
	UPROPERTY(Replicated) int32 Team = -1;

	TWeakObjectPtr<ABotSCombatCharacter> Source;
	TMap<FName, float> Params;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	float Travelled = 0.f;
	TArray<TWeakObjectPtr<AActor>> HitActors;
};

/** Flaeche mit Vorwarnzeit und sichtbarem Rand (GDD Abschnitt 11). */
UCLASS()
class ABotSAreaEffect : public AActor
{
	GENERATED_BODY()

public:
	ABotSAreaEffect();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated) float Radius = 3.f;
	UPROPERTY(Replicated) float Delay = 0.5f;
	UPROPERTY(Replicated) float Duration = 0.f;
	UPROPERTY(Replicated) int32 Team = -1;

	float Interval = 0.5f;
	bool bFollow = false;
	TWeakObjectPtr<ABotSCombatCharacter> Source;
	TMap<FName, float> Params;

private:
	void Pulse();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	float Age = 0.f;
	float NextPulse = 0.f;
	bool bArmedVisual = false;
};
