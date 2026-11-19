#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemInterface.h"
#include "Core/BotSTypes.h"
#include "BotSCharacters.generated.h"

class UBotSAbilitySystemComponent;
class UBotSAttributeSet;
class ABotSPlayerState;
class UCameraComponent;
class UStaticMeshComponent;

/** Gemeinsame Basis fuer Helden und neutrale Kreaturen: alles, was Schaden nehmen kann. */
UCLASS(Abstract)
class ABotSCombatCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABotSCombatCharacter(const FObjectInitializer& ObjectInitializer);

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return nullptr; }
	virtual const UBotSAttributeSet* GetAttributes() const { return nullptr; }
	/** Truppnummer, -1 fuer neutrale Kreaturen. */
	virtual int32 GetTeam() const { return -1; }
	virtual bool IsTargetable() const { return true; }
	virtual bool IsDowned() const { return false; }

	float GetHealth() const;
	float GetMaxHealth() const;
	float GetShield() const;
	bool HasTag(const FGameplayTag& Tag) const;
	/** Server: verschiebt den Charakter um Meter, haelt an Waenden. */
	void Shove(const FVector& Direction, float DistanceM);
	void SetBodyColor(const FLinearColor& Color);

protected:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> NoseMesh;
};

/** Bewegungsvorhersage einschliesslich Sturmlauf (TDD Abschnitt 5). */
UCLASS()
class UBotSCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

	/** Wunsch des Clients, in jedem gespeicherten Bewegungsschritt uebertragen. */
	uint8 bWantsStormRun : 1 = false;
};

UCLASS()
class ABotSHeroCharacter : public ABotSCombatCharacter
{
	GENERATED_BODY()

public:
	ABotSHeroCharacter(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual const UBotSAttributeSet* GetAttributes() const override;
	virtual int32 GetTeam() const override;
	virtual bool IsTargetable() const override;
	virtual bool IsDowned() const override;

	ABotSPlayerState* GetBotSPlayerState() const;
	UBotSCharacterMovementComponent* GetBotSMovement() const;

	// ---- Server ----
	bool TryUseSlot(EBotSSlot Slot, const FVector& Aim);
	void SetAim(const FVector& Aim) { AimPoint = Aim; }
	const FVector& GetAim() const { return AimPoint; }
	void SetAttacking(bool bNew) { bAttacking = bNew; }
	void SetInteractHeld(bool bHeld);
	void StartStormRun();
	void CancelStormRun();
	/** Unterbricht Sturmlauf und Wiederbelebung. Liefert den Faktor fuer den Basisangriff (Merkmal Kombo). */
	float NotifyAbilityUsed(EBotSSlot Slot);
	void NotifyDamaged();
	void RefreshAppearance();

	// ---- Eigener Client ----
	void LocalBeginStormRun();
	void LocalCancelStormRun();
	bool IsLocalStormRunPending() const { return LocalStormRunStart > 0.f; }

	/** Vom Server freigegebener Sturmlauf. */
	UPROPERTY(ReplicatedUsing = OnRep_StormRunAllowed) bool bStormRunAllowed = false;
	/** Fortschritt einer laufenden Wiederbelebung, 0 = keine. Server-Zeit des Endes. */
	UPROPERTY(Replicated) float ReviveEndTime = 0.f;
	UPROPERTY(Replicated) float ReviveDuration = 0.f;
	UPROPERTY(Replicated) float StormRunChannelEnd = 0.f;

protected:
	UFUNCTION() void OnRep_StormRunAllowed();
	void InitFromPlayerState();
	void ServerTick(float DeltaSeconds);
	void TickRevive(float DeltaSeconds);
	void TryInteract();

	FVector AimPoint = FVector::ZeroVector;
	bool bAttacking = false;
	bool bInteractHeld = false;
	TWeakObjectPtr<ABotSHeroCharacter> ReviveTarget;
	FVector ReviveStartLocation = FVector::ZeroVector;
	float CritReadyUntil = 0.f;
	float LocalStormRunStart = 0.f;
	float LocalStormRunWantTime = 0.f;
	bool bAppearanceSet = false;
};

/** Neutrale Kreatur eines Lagers: wartet, greift an, kehrt zurueck (TDD Abschnitt 10). */
UCLASS()
class ABotSCreature : public ABotSCombatCharacter
{
	GENERATED_BODY()

public:
	ABotSCreature(const FObjectInitializer& ObjectInitializer);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual const UBotSAttributeSet* GetAttributes() const override { return Attributes; }

	FVector Home = FVector::ZeroVector;

protected:
	void HandleDamaged(float Damage, AActor* InstigatorActor, bool bStorm);
	void HandleOutOfHealth(AActor* InstigatorActor);

	UPROPERTY() TObjectPtr<UBotSAbilitySystemComponent> AbilitySystem;
	UPROPERTY() TObjectPtr<UBotSAttributeSet> Attributes;
	TWeakObjectPtr<ABotSCombatCharacter> Target;
	float NextAttackTime = 0.f;
	bool bReturning = false;
	bool bDead = false;
};

/** Top-Down-Kamera, existiert nur auf dem eigenen Client (TDD Abschnitt 7). */
UCLASS()
class ABotSCameraRig : public AActor
{
	GENERATED_BODY()

public:
	ABotSCameraRig();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<AActor> FollowTarget;
	FVector AimPoint = FVector::ZeroVector;
	bool bHasAim = false;

private:
	UPROPERTY() TObjectPtr<UCameraComponent> Camera;
	FVector LeadOffset = FVector::ZeroVector;
	bool bSnapped = false;
};
