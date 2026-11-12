#include "Items/BotSItems.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Character/BotSCharacters.h"
#include "Match/BotSMatch.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------------
// Inventar
// ---------------------------------------------------------------------------------------------
UBotSInventoryComponent::UBotSInventoryComponent()
{
	SetIsReplicatedByDefault(true);
	Relics.SetNum(3);
	Actives.SetNum(2);
}

void UBotSInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBotSInventoryComponent, Relics);
	DOREPLIFETIME(UBotSInventoryComponent, Actives);
}

bool UBotSInventoryComponent::AddItem(FName ItemId, int32 Charges, FBotSItemSlot& OutDropped)
{
	OutDropped = FBotSItemSlot();
	const FBotSItemDef* Def = BotSData().FindItem(ItemId);
	if (!Def) { return false; }

	if (Def->Kind != EBotSItemKind::Active)
	{
		// Ein Slot je Relikt-Typ. Ist er belegt, faellt das alte Relikt zu Boden.
		FBotSItemSlot& Slot = Relics[int32(Def->Kind)];
		if (Slot.Item == ItemId) { return false; }
		OutDropped = Slot;
		Slot.Item = ItemId;
		Slot.Charges = 1;
		return true;
	}

	for (FBotSItemSlot& Slot : Actives)
	{
		if (Slot.Item == ItemId)
		{
			if (Slot.Charges >= Def->MaxCharges) { return false; }
			Slot.Charges = FMath::Min(Def->MaxCharges, Slot.Charges + FMath::Max(1, Charges));
			return true;
		}
	}
	for (FBotSItemSlot& Slot : Actives)
	{
		if (Slot.Item.IsNone())
		{
			Slot.Item = ItemId;
			Slot.Charges = FMath::Max(1, Charges);
			return true;
		}
	}
	FBotSItemSlot& Replace = Actives[0].Charges <= Actives[1].Charges ? Actives[0] : Actives[1];
	OutDropped = Replace;
	Replace.Item = ItemId;
	Replace.Charges = FMath::Max(1, Charges);
	return true;
}

bool UBotSInventoryComponent::ConsumeActive(int32 Index)
{
	if (!Actives.IsValidIndex(Index) || Actives[Index].Item.IsNone() || Actives[Index].Charges <= 0) { return false; }
	if (--Actives[Index].Charges <= 0) { Actives[Index] = FBotSItemSlot(); }
	return true;
}

void UBotSInventoryComponent::DropAll(const FVector& Location)
{
	for (const FBotSItemSlot& Slot : Relics) { if (!Slot.Item.IsNone()) { ABotSPickup::SpawnPickup(GetWorld(), Location, Slot.Item, 1); } }
	for (const FBotSItemSlot& Slot : Actives) { if (!Slot.Item.IsNone()) { ABotSPickup::SpawnPickup(GetWorld(), Location, Slot.Item, Slot.Charges); } }
	Clear();
}

void UBotSInventoryComponent::Clear()
{
	for (FBotSItemSlot& Slot : Relics) { Slot = FBotSItemSlot(); }
	for (FBotSItemSlot& Slot : Actives) { Slot = FBotSItemSlot(); }
}

float UBotSInventoryComponent::GetStat(FName Stat) const
{
	float Sum = 0.f;
	for (const FBotSItemSlot& Slot : Relics)
	{
		const FBotSItemDef* Def = Slot.Item.IsNone() ? nullptr : BotSData().FindItem(Slot.Item);
		if (Def) { Sum += Def->Stats.FindRef(Stat); }
	}
	return Sum;
}

const FBotSItemDef* UBotSInventoryComponent::GetActiveDef(int32 Index) const
{
	return (Actives.IsValidIndex(Index) && !Actives[Index].Item.IsNone() && Actives[Index].Charges > 0) ? BotSData().FindItem(Actives[Index].Item) : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Aufhebbarer Gegenstand
// ---------------------------------------------------------------------------------------------
ABotSPickup::ABotSPickup()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>("Mesh");
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
}

void ABotSPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSPickup, ItemId);
	DOREPLIFETIME(ABotSPickup, Charges);
}

void ABotSPickup::BeginPlay()
{
	Super::BeginPlay();
	Mesh->SetStaticMesh(BotSCombat::Shape(TEXT("Cube")));
	Mesh->SetWorldScale3D(FVector(0.4f));
	const FBotSItemDef* Def = BotSData().FindItem(ItemId);
	FLinearColor Color(0.9f, 0.9f, 0.9f);
	if (Def && Def->Kind != EBotSItemKind::Active) { Color = Def->Rarity > 0 ? FLinearColor(0.3f, 0.5f, 1.f) : FLinearColor(0.3f, 0.8f, 0.3f); }
	BotSCombat::SetMeshColor(Mesh, Color);
}

ABotSPickup* ABotSPickup::SpawnPickup(UWorld* World, const FVector& Location, FName ItemId, int32 Charges)
{
	const FVector2D Offset = FMath::RandPointInCircle(90.f);
	const FVector Pos(Location.X + Offset.X, Location.Y + Offset.Y, 30.f);
	ABotSPickup* Pickup = World->SpawnActorDeferred<ABotSPickup>(ABotSPickup::StaticClass(), FTransform(Pos), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Pickup->ItemId = ItemId;
	Pickup->Charges = FMath::Max(1, Charges);
	Pickup->FinishSpawning(FTransform(Pos));
	return Pickup;
}

// ---------------------------------------------------------------------------------------------
// Truhe
// ---------------------------------------------------------------------------------------------
ABotSLootChest::ABotSLootChest()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>("Mesh");
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RootComponent = Mesh;
}

void ABotSLootChest::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABotSLootChest, bOpened);
}

void ABotSLootChest::BeginPlay()
{
	Super::BeginPlay();
	Mesh->SetStaticMesh(BotSCombat::Shape(TEXT("Cube")));
	Mesh->SetWorldScale3D(FVector(1.2f, 0.8f, 0.8f));
	OnRep_Opened();
}

void ABotSLootChest::OnRep_Opened()
{
	BotSCombat::SetMeshColor(Mesh, bOpened ? FLinearColor(0.12f, 0.09f, 0.03f) : FLinearColor(1.f, 0.7f, 0.1f));
}

void ABotSLootChest::Open()
{
	if (!HasAuthority() || bOpened) { return; }
	bOpened = true;
	OnRep_Opened();
	const FBotSGameData& Data = BotSData();
	FRandomStream Rng(FMath::Rand());
	for (int32 I = 0; I < Data.MatchRules.ChestItems; ++I)
	{
		const FName Item = FBotSRules::RollLoot(Data.ChestLoot, Rng);
		if (const FBotSItemDef* Def = Data.FindItem(Item)) { ABotSPickup::SpawnPickup(GetWorld(), GetActorLocation(), Item, Def->Charges); }
	}
	UE_LOG(LogBotSItems, Log, TEXT("Truhe geoeffnet bei %s"), *GetActorLocation().ToCompactString());
}

// ---------------------------------------------------------------------------------------------
// Sturm
// ---------------------------------------------------------------------------------------------
ABotSStormController::ABotSStormController()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void ABotSStormController::StartStorm()
{
	Rng.GenerateNewSeed();
	bRunning = true;
	BeginPhase(1, FVector2D::ZeroVector, BotSData().Tuning.StormStartDiameter * 0.5f);
}

void ABotSStormController::BeginPhase(int32 Phase, FVector2D FromCenter, float FromRadius)
{
	ABotSGameState* G = GetWorld()->GetGameState<ABotSGameState>();
	const FBotSGameData& Data = BotSData();
	if (!G || !Data.StormPhases.IsValidIndex(Phase - 1)) { return; }
	const FBotSStormPhase& Def = Data.StormPhases[Phase - 1];
	const float NewRadius = Def.Diameter * 0.5f;
	const float Now = G->GetServerWorldTimeSeconds();

	FBotSStormState S;
	S.Phase = Phase;
	S.NumPhases = Data.StormPhases.Num();
	S.FromCenter = FromCenter;
	S.FromRadius = FromRadius;
	S.ToCenter = FBotSRules::PickEye(Rng, FromCenter, FromRadius, NewRadius);
	// Das Auge bleibt auf der Karte.
	const float Limit = FMath::Max(0.f, FBotSMapLayout::Get().Half - NewRadius);
	S.ToCenter.X = FMath::Clamp(S.ToCenter.X, -Limit, Limit);
	S.ToCenter.Y = FMath::Clamp(S.ToCenter.Y, -Limit, Limit);
	S.ToRadius = NewRadius;
	S.ShrinkStart = Now + Def.Wait;
	S.ShrinkEnd = S.ShrinkStart + Def.Shrink;
	S.DamagePct = Def.DamagePct;
	G->Storm = S;
	G->ForceNetUpdate();
	UE_LOG(LogBotSStorm, Log, TEXT("Sturmphase %d: Auge (%.0f, %.0f) Radius %.0f m, Warten %.0f s, Schrumpfen %.0f s, Schaden %.0f %%/s"), Phase, S.ToCenter.X, S.ToCenter.Y, NewRadius, Def.Wait, Def.Shrink, Def.DamagePct);
}

void ABotSStormController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABotSGameState* G = GetWorld()->GetGameState<ABotSGameState>();
	if (!bRunning || !G || G->Phase != EBotSMatchPhase::Match) { return; }
	const float Now = G->GetServerWorldTimeSeconds();
	if (Now >= G->Storm.ShrinkEnd && G->Storm.Phase < G->Storm.NumPhases)
	{
		BeginPhase(G->Storm.Phase + 1, G->Storm.ToCenter, G->Storm.ToRadius);
	}
	if (Now < NextDamageTime) { return; }
	NextDamageTime = Now + 1.f;

	FVector2D Center; float Radius;
	G->GetStormCircle(Center, Radius);
	const TArray<TObjectPtr<APlayerState>> Players = G->PlayerArray;
	for (APlayerState* Base : Players)
	{
		ABotSPlayerState* PS = Cast<ABotSPlayerState>(Base);
		ABotSHeroCharacter* Hero = PS ? PS->GetHero() : nullptr;
		if (!Hero || !PS->IsInMatch()) { continue; }
		const bool bOutside = FVector2D::Distance(BotSToM(Hero->GetActorLocation()), Center) > Radius;
		PS->SetInStorm(bOutside);
		if (bOutside) { BotSCombat::ApplyDamage(nullptr, Hero, Hero->GetMaxHealth() * G->Storm.DamagePct * 0.01f, true); }
	}
}

// ---------------------------------------------------------------------------------------------
// Sturmwand
// ---------------------------------------------------------------------------------------------
static const int32 BotSWallSegments = 96;
static const int32 BotSEyeSegments = 64;

ABotSStormWall::ABotSStormWall()
{
	PrimaryActorTick.bCanEverTick = true;
	Segments = CreateDefaultSubobject<UInstancedStaticMeshComponent>("Segments");
	Segments->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Segments->SetCastShadow(false);
	RootComponent = Segments;
	EyeSegments = CreateDefaultSubobject<UInstancedStaticMeshComponent>("EyeSegments");
	EyeSegments->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	EyeSegments->SetCastShadow(false);
	EyeSegments->SetupAttachment(RootComponent);
}

void ABotSStormWall::BeginPlay()
{
	Super::BeginPlay();
	Segments->SetStaticMesh(BotSCombat::Shape(TEXT("Cube")));
	EyeSegments->SetStaticMesh(BotSCombat::Shape(TEXT("Cube")));
	BotSCombat::SetMeshColor(Segments, FLinearColor(0.3f, 0.1f, 0.6f));
	BotSCombat::SetMeshColor(EyeSegments, FLinearColor(0.9f, 0.9f, 1.f));
	for (int32 I = 0; I < BotSWallSegments; ++I) { Segments->AddInstance(FTransform::Identity, true); }
	for (int32 I = 0; I < BotSEyeSegments; ++I) { EyeSegments->AddInstance(FTransform::Identity, true); }
}

static void BotSLayoutRing(UInstancedStaticMeshComponent* Mesh, int32 Count, FVector2D CenterM, float RadiusM, float Height, float Thickness, float Z)
{
	const bool bVisible = RadiusM > 0.25f;
	Mesh->SetVisibility(bVisible);
	if (!bVisible) { return; }
	const float Length = 2.f * PI * RadiusM / Count * 1.05f;
	for (int32 I = 0; I < Count; ++I)
	{
		const float Angle = 2.f * PI * I / Count;
		const FVector Pos = BotSM(CenterM + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * RadiusM, Z);
		const FTransform T(FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f), Pos, FVector(Length, Thickness, Height));
		Mesh->UpdateInstanceTransform(I, T, true, I == Count - 1, true);
	}
}

void ABotSStormWall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const ABotSGameState* G = GetWorld()->GetGameState<ABotSGameState>();
	if (!G || G->Phase < EBotSMatchPhase::Match)
	{
		Segments->SetVisibility(false);
		EyeSegments->SetVisibility(false);
		return;
	}
	FVector2D Center; float Radius;
	G->GetStormCircle(Center, Radius);
	// Der Rand der Zone muss immer klar erkennbar sein (GDD Abschnitt 13).
	BotSLayoutRing(Segments, BotSWallSegments, Center, Radius, 6.f, 0.8f, 300.f);
	BotSLayoutRing(EyeSegments, BotSEyeSegments, G->Storm.ToCenter, G->Storm.Phase > 0 ? G->Storm.ToRadius : 0.f, 0.1f, 0.4f, 8.f);
}

// ---------------------------------------------------------------------------------------------
// Graubox
// ---------------------------------------------------------------------------------------------
ABotSMapGeometry::ABotSMapGeometry()
{
	Floor = CreateDefaultSubobject<UInstancedStaticMeshComponent>("Floor");
	RootComponent = Floor;
	Walls = CreateDefaultSubobject<UInstancedStaticMeshComponent>("Walls");
	Walls->SetupAttachment(RootComponent);
	KeyLight = CreateDefaultSubobject<UDirectionalLightComponent>("KeyLight");
	KeyLight->SetupAttachment(RootComponent);
	KeyLight->SetMobility(EComponentMobility::Movable);
	KeyLight->SetRelativeRotation(FRotator(-55.f, 35.f, 0.f));
	KeyLight->SetIntensity(6.f);
	FillLight = CreateDefaultSubobject<UDirectionalLightComponent>("FillLight");
	FillLight->SetupAttachment(RootComponent);
	FillLight->SetMobility(EComponentMobility::Movable);
	FillLight->SetRelativeRotation(FRotator(-45.f, -140.f, 0.f));
	FillLight->SetIntensity(2.5f);
	FillLight->SetCastShadows(false);
}

void ABotSMapGeometry::BeginPlay()
{
	Super::BeginPlay();
	// Gleicher Name auf Server und Clients: Charaktere koennen den Boden als Bewegungsbasis ueber das Netz benennen.
	SetNetAddressable();
	const FBotSMapLayout& Layout = FBotSMapLayout::Get();
	const FBotSMapDef& Def = BotSData().Map;
	UStaticMesh* Cube = BotSCombat::Shape(TEXT("Cube"));

	Floor->SetStaticMesh(Cube);
	BotSCombat::SetMeshColor(Floor, FLinearColor(0.16f, 0.19f, 0.17f));
	const float FloorSize = Layout.Half * 2.f + 300.f;
	Floor->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -50.f), FVector(FloorSize, FloorSize, 1.f)), true);

	Walls->SetStaticMesh(Cube);
	BotSCombat::SetMeshColor(Walls, FLinearColor(0.5f, 0.46f, 0.42f));
	for (const FBox2D& Box : Layout.Walls)
	{
		const FVector2D Center = Box.GetCenter(), Size = Box.GetSize();
		Walls->AddInstance(FTransform(FRotator::ZeroRotator, BotSM(Center, BotSM(Def.WallHeight) * 0.5f), FVector(Size.X, Size.Y, Def.WallHeight)), true);
	}
	UE_LOG(LogBotS, Log, TEXT("Karte erzeugt: %d Wandquader, %d Truhenplaetze, %d Lager, %d Startpunkte"), Layout.Walls.Num(), Layout.Chests.Num(), Layout.Camps.Num(), Layout.Spawns.Num());
}
