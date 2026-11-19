#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Info.h"
#include "AbilitySystemInterface.h"
#include "Core/BotSTypes.h"
#include "BotSMatch.generated.h"

class UBotSAbilitySystemComponent;
class UBotSAttributeSet;
class UBotSInventoryComponent;
class ABotSHeroCharacter;
class ABotSSquadState;
class ABotSStormController;

/** Dauerhafte Daten eines Spielers: Held, Trupp, Faehigkeiten, Attribute, Inventar, Talente (TDD Abschnitt 4). */
UCLASS()
class ABotSPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ABotSPlayerState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	UBotSAbilitySystemComponent* GetASC() const { return AbilitySystem; }
	const UBotSAttributeSet* GetAttributes() const { return Attributes; }
	UBotSInventoryComponent* GetInventory() const { return Inventory; }
	ABotSHeroCharacter* GetHero() const;
	const FBotSHeroDef* GetHeroDef() const;
	/** Faehigkeit eines Slots mit angewendeten Talenten. Slots Item1 und Item2 liefern die Faehigkeit des Gegenstands. */
	bool GetAbilityDef(EBotSSlot Slot, FBotSAbilityDef& Out) const;
	int32 GetLevel() const;
	float GetLevelFactor() const;
	/** Erste Talentstufe, deren Level erreicht und die noch offen ist, sonst -1. */
	int32 GetOpenTalentTier() const;
	/** Summe der Anteile aus Talenten und Relikten fuer einen Heldenwert. */
	float GetStatBonus(FName Stat) const;
	float GetCooldownRemaining(EBotSSlot Slot, float& OutDuration) const;
	bool IsAlive() const { return LifeState == EBotSLifeState::Alive; }
	bool IsInMatch() const { return LifeState == EBotSLifeState::Alive || LifeState == EBotSLifeState::Downed; }

	// ---- Server ----
	void InitForMatch();
	void RecomputeStats();
	bool ChooseTalent(int32 Tier, int32 Option);
	void AddShield(float Amount, float Duration);
	void AddHealOverTime(float Total, float Duration);
	void SetInStorm(bool bNew);
	void Revive();
	void Eliminate(AActor* InstigatorActor);
	void NotifyCombat();

	UPROPERTY(Replicated) FName HeroId;
	UPROPERTY(Replicated) int32 SquadIndex = -1;
	UPROPERTY(Replicated) TObjectPtr<ABotSSquadState> Squad;
	UPROPERTY(Replicated) EBotSLifeState LifeState = EBotSLifeState::None;
	UPROPERTY(Replicated) float DownedEndTime = 0.f;
	/** Gewaehlte Option je Talentstufe, -1 = offen. */
	UPROPERTY(Replicated) TArray<int8> TalentChoices;
	UPROPERTY(Replicated) int32 SpawnChoice = -1;
	UPROPERTY(Replicated) bool bInStorm = false;

protected:
	void HandleDamaged(float Damage, AActor* InstigatorActor, bool bStorm);
	void HandleOutOfHealth(AActor* InstigatorActor);
	void GoDown(AActor* InstigatorActor);

	UPROPERTY() TObjectPtr<UBotSAbilitySystemComponent> AbilitySystem;
	UPROPERTY() TObjectPtr<UBotSAttributeSet> Attributes;
	UPROPERTY() TObjectPtr<UBotSInventoryComponent> Inventory;

	float ShieldExpireTime = 0.f;
	float HotRemaining = 0.f;
	float HotRate = 0.f;
	float RegenAccum = 0.f;
	float LastCombatTime = -100.f;
	bool bAbilitiesGranted = false;
};

/** Trupp-Daten: Mitglieder, gemeinsame Erfahrung, gemeinsames Level. */
UCLASS()
class ABotSSquadState : public AInfo
{
	GENERATED_BODY()

public:
	ABotSSquadState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void AddXp(float Amount);
	int32 NumAlive() const;
	int32 NumInMatch() const;

	UPROPERTY(Replicated) int32 SquadIndex = 0;
	UPROPERTY(Replicated) TArray<TObjectPtr<ABotSPlayerState>> Members;
	UPROPERTY(Replicated) int32 Level = 1;
	UPROPERTY(Replicated) float Xp = 0.f;
	UPROPERTY(Replicated) bool bEliminated = false;
	UPROPERTY(Replicated) int32 Placement = 0;
};

UCLASS()
class ABotSGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ABotSGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	void GetStormCircle(FVector2D& OutCenter, float& OutRadius) const;
	int32 NumSquadsAlive() const;
	int32 NumPlayersAlive() const;
	float GetPhaseTimeLeft() const { return FMath::Max(0.f, PhaseEndTime - float(GetServerWorldTimeSeconds())); }

	UPROPERTY(Replicated) EBotSMatchPhase Phase = EBotSMatchPhase::WaitingForPlayers;
	UPROPERTY(Replicated) float PhaseEndTime = 0.f;
	UPROPERTY(Replicated) TArray<TObjectPtr<ABotSSquadState>> Squads;
	UPROPERTY(Replicated) FBotSStormState Storm;
	UPROPERTY(Replicated) int32 WinnerSquad = -1;
};

/** Match-Phasen, Truppzuteilung, Startpunkte, Siegpruefung (TDD Abschnitt 8). Nur Server. */
UCLASS()
class ABotSGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABotSGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void Tick(float DeltaSeconds) override;

	void ForceStart();
	bool SelectHero(ABotSPlayerState* PS, FName HeroId);
	void SelectSpawn(ABotSPlayerState* PS, int32 SpawnIndex);
	void OnPlayerDowned(ABotSPlayerState* PS);
	void OnPlayerEliminated(ABotSPlayerState* PS);

protected:
	ABotSGameState* GS() const { return GetGameState<ABotSGameState>(); }
	void SetPhase(EBotSMatchPhase NewPhase, float Duration);
	void AssignSquad(ABotSPlayerState* PS);
	void StartHeroSelect();
	void StartSpawnSelect();
	void StartMatch();
	void CheckSquad(ABotSSquadState* Squad);
	void CheckWin();
	void EndMatch(int32 WinnerSquad);
	int32 NumHumans() const;

	UPROPERTY() TObjectPtr<ABotSStormController> StormController;
	FRandomStream Rng;
	bool bForceStart = false;
	bool bAllowBots = true;
	bool bEarlyFinishArmed = false;
	int32 LastEliminatedSquad = -1;
};
