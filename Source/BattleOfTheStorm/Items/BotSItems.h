#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Info.h"
#include "Core/BotSTypes.h"
#include "BotSItems.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;

USTRUCT()
struct FBotSItemSlot
{
	GENERATED_BODY()
	UPROPERTY() FName Item;
	UPROPERTY() int32 Charges = 0;
};

/** 3 Relikt-Slots (einer je Typ) und 2 Aktiv-Slots (TDD Abschnitt 10). */
UCLASS()
class UBotSInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBotSInventoryComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server. Liefert in OutDropped den Gegenstand, der dafuer zu Boden faellt. */
	bool AddItem(FName ItemId, int32 Charges, FBotSItemSlot& OutDropped);
	bool ConsumeActive(int32 Index);
	void DropAll(const FVector& Location);
	void Clear();
	float GetStat(FName Stat) const;
	const FBotSItemDef* GetActiveDef(int32 Index) const;

	UPROPERTY(Replicated) TArray<FBotSItemSlot> Relics;
	UPROPERTY(Replicated) TArray<FBotSItemSlot> Actives;
};

UCLASS()
class ABotSPickup : public AActor
{
	GENERATED_BODY()

public:
	ABotSPickup();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	static ABotSPickup* SpawnPickup(UWorld* World, const FVector& Location, FName ItemId, int32 Charges);

	UPROPERTY(Replicated) FName ItemId;
	UPROPERTY(Replicated) int32 Charges = 1;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class ABotSLootChest : public AActor
{
	GENERATED_BODY()

public:
	ABotSLootChest();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	void Open();

	UPROPERTY(ReplicatedUsing = OnRep_Opened) bool bOpened = false;

private:
	UFUNCTION() void OnRep_Opened();
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
};

/** Sturmphasen, Wahl des Auges, Sturmschaden (TDD Abschnitt 9). Nur Server. */
UCLASS()
class ABotSStormController : public AInfo
{
	GENERATED_BODY()

public:
	ABotSStormController();
	virtual void Tick(float DeltaSeconds) override;
	void StartStorm();

private:
	void BeginPhase(int32 Phase, FVector2D FromCenter, float FromRadius);
	FRandomStream Rng;
	float NextDamageTime = 0.f;
	bool bRunning = false;
};

/** Sturmwand: Ring aus Segmenten, den der Client auf den errechneten Kreis setzt. Nur eigener Client. */
UCLASS()
class ABotSStormWall : public AActor
{
	GENERATED_BODY()

public:
	ABotSStormWall();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Segments;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> EyeSegments;
};

/** Graubox der Karte: Boden, Waende und Licht, auf Server und Clients aus denselben Daten erzeugt. */
UCLASS()
class ABotSMapGeometry : public AActor
{
	GENERATED_BODY()

public:
	ABotSMapGeometry();
	virtual void BeginPlay() override;

private:
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Floor;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Walls;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UDirectionalLightComponent> FillLight;
};
