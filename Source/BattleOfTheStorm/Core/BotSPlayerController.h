#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/HUD.h"
#include "AIController.h"
#include "Components/ActorComponent.h"
#include "Core/BotSTypes.h"
#include "BotSPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class ABotSCameraRig;
class ABotSStormWall;
class ABotSHeroCharacter;
class ABotSPlayerState;
class ABotSGameState;
class ABotSCombatCharacter;
class UBotSVisionComponent;

/** Sichtlinie und geteilte Truppsicht, auf dem Client berechnet (TDD Abschnitt 11). */
UCLASS()
class UBotSVisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBotSVisionComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};

UCLASS()
class ABotSPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABotSPlayerController();
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	ABotSPlayerState* GetBotSPlayerState() const;
	ABotSHeroCharacter* GetHero() const;
	const FVector& GetLocalAim() const { return LocalAim; }

	UFUNCTION(Server, Unreliable) void ServerSetAim(FVector_NetQuantize Aim, bool bAttacking);
	UFUNCTION(Server, Reliable) void ServerUseSlot(uint8 Slot, FVector_NetQuantize Aim);
	UFUNCTION(Server, Reliable) void ServerStormRun();
	UFUNCTION(Server, Reliable) void ServerInteract(bool bHeld);
	UFUNCTION(Server, Reliable) void ServerSelectHero(FName HeroId);
	UFUNCTION(Server, Reliable) void ServerSelectSpawn(int32 SpawnIndex);
	UFUNCTION(Server, Reliable) void ServerChooseTalent(int32 Tier, int32 Option);
	UFUNCTION(Server, Reliable) void ServerForceStart();
	/** Konsolenbefehl: startet das Match sofort und fuellt mit Bots auf. */
	UFUNCTION(Exec) void BotSStart();

	bool bShowMap = false;

protected:
	void OnSlotPressed(EBotSSlot Slot);
	void OnAttack(bool bPressed);
	UInputAction* AddAction(FName Name, FName DefaultKey);

	UPROPERTY() TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> Actions;
	UPROPERTY() TObjectPtr<ABotSCameraRig> CameraRig;
	UPROPERTY() TObjectPtr<ABotSStormWall> StormWall;
	UPROPERTY() TObjectPtr<UBotSVisionComponent> Vision;

	FVector LocalAim = FVector::ZeroVector;
	FVector2D MoveInput = FVector2D::ZeroVector;
	bool bMoveUp = false, bMoveDown = false, bMoveLeft = false, bMoveRight = false;
	bool bAttackHeld = false;
	float NextAimSend = 0.f;
};

/** Oberflaeche des Prototyps, mit Canvas gezeichnet (Abweichung vom TDD: keine UMG-Widgets). */
UCLASS()
class ABotSHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
	/** Liefert true, wenn der Klick eine Schaltflaeche getroffen hat. */
	bool HandleClick(const FVector2D& ScreenPos);

private:
	void DrawWorldBars(ABotSPlayerState* MyPS);
	void DrawAbilityBar(ABotSPlayerState* MyPS);
	void DrawSquadFrames(ABotSPlayerState* MyPS);
	void DrawStormStatus(ABotSGameState* GS, ABotSPlayerState* MyPS);
	void DrawMap(ABotSGameState* GS, ABotSPlayerState* MyPS, float X, float Y, float Size, bool bSpawnSelect);
	void DrawTalentChoice(ABotSPlayerState* MyPS);
	void DrawHeroSelect(ABotSGameState* GS, ABotSPlayerState* MyPS);
	void DrawMatchEnd(ABotSGameState* GS, ABotSPlayerState* MyPS);
	void DrawPrompts(ABotSPlayerState* MyPS);

	void Text(const FString& S, float X, float Y, const FLinearColor& Color = FLinearColor::White, float Scale = 1.f, bool bCenter = false);
	void Box(float X, float Y, float W, float H, const FLinearColor& Color);
	void Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Back);
	void Circle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness = 1.f);
	void Button(const FString& Label, float X, float Y, float W, float H, FName Id, const FLinearColor& Color);

	struct FHit { FBox2D Rect; FName Id; };
	TArray<FHit> Hits;
};

/** Test-Bot: steuert einen Helden, folgt dem Auge, sammelt Beute und kaempft (TDD Abschnitt 16). Kein Spielinhalt. */
UCLASS()
class ABotSBotController : public AAIController
{
	GENERATED_BODY()

public:
	ABotSBotController();
	virtual void Tick(float DeltaSeconds) override;

private:
	void Think(ABotSHeroCharacter* Hero, ABotSPlayerState* PS);
	void MoveTowards(ABotSHeroCharacter* Hero, const FVector& Goal, float DeltaSeconds);
	void TryAbilities(ABotSHeroCharacter* Hero, ABotSPlayerState* PS, ABotSCombatCharacter* Enemy, float EnemyDistM);
	bool HasLineOfSight(const FVector& From, const FVector& To) const;

	FVector Goal = FVector::ZeroVector;
	bool bHasGoal = false;
	bool bWantStormRun = false;
	TArray<FVector2D> Path;
	FVector2D PathGoal = FVector2D::ZeroVector;
	float NextThink = 0.f;
	float NextRepath = 0.f;
	float NextInteract = 0.f;
	FVector WanderOffset = FVector::ZeroVector;
	float NextWander = 0.f;
};
