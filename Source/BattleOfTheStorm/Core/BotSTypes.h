#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "BotSTypes.generated.h"

// ---------------------------------------------------------------------------------------------
// Gameplay Tags (TDD Abschnitt 6)
// ---------------------------------------------------------------------------------------------
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Downed);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_InStorm);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Channeling);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Stunned);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Rooted);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_StormRun);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Invulnerable);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Damage_Storm);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Heal);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Mana);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Duration);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_MoveSpeedMul);
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_AttackSpeedMul);

// ---------------------------------------------------------------------------------------------
// Aufzaehlungen
// ---------------------------------------------------------------------------------------------
UENUM()
enum class EBotSRole : uint8 { Tank, Fighter, Healer, Support, RangedAssassin, MeleeAssassin };

UENUM()
enum class EBotSAbilityType : uint8 { Melee, Projectile, Area, Dash, SelfBuff };

/** Slots laut GDD Abschnitt 6. Der Wert ist zugleich die InputID der gewaehrten Faehigkeit. */
UENUM()
enum class EBotSSlot : uint8 { Basic, Ability1, Ability2, Ability3, Ultimate, Trait, StormRun, Item1, Item2, Count };

UENUM()
enum class EBotSItemKind : uint8 { RelicAttack, RelicDefense, RelicUtility, Active };

UENUM()
enum class EBotSMatchPhase : uint8 { WaitingForPlayers, HeroSelect, SpawnSelect, Match, Ended };

UENUM()
enum class EBotSLifeState : uint8 { None, Alive, Downed, Eliminated };

// ---------------------------------------------------------------------------------------------
// Daten (TDD Abschnitt 13). Quelle ist Content/BotS/Data/GameData.json.
// ---------------------------------------------------------------------------------------------
USTRUCT()
struct FBotSAbilityDef
{
	GENERATED_BODY()
	UPROPERTY() FName Id;
	UPROPERTY() FString Name;
	UPROPERTY() FString Description;
	UPROPERTY() EBotSAbilityType Type = EBotSAbilityType::Melee;
	UPROPERTY() float Cooldown = 0.f;
	UPROPERTY() float ManaCost = 0.f;
	/** Alle Zahlenwerte der Faehigkeit. Talente veraendern diese Werte (siehe FBotSTalentMod). */
	UPROPERTY() TMap<FName, float> Params;

	float P(FName Key, float Default = 0.f) const { const float* V = Params.Find(Key); return V ? *V : Default; }
	bool IsValid() const { return !Id.IsNone(); }
};

USTRUCT()
struct FBotSTalentMod
{
	GENERATED_BODY()
	/** Id der Faehigkeit. Leer: Param ist ein Heldenwert (MaxHealth, AttackDamage, AttackSpeed, MoveSpeed, CooldownReduction, ManaRegen, HealthRegen). */
	UPROPERTY() FName Ability;
	/** Name des Werts, oder "Cooldown" und "ManaCost". */
	UPROPERTY() FName Param;
	/** Add, Mul oder Set. Bei Heldenwerten ist der Wert immer ein Anteil (0.15 = +15 %). */
	UPROPERTY() FName Op = "Add";
	UPROPERTY() float Value = 0.f;
};

USTRUCT()
struct FBotSTalentDef
{
	GENERATED_BODY()
	UPROPERTY() FName Id;
	UPROPERTY() FString Name;
	UPROPERTY() FString Description;
	UPROPERTY() TArray<FBotSTalentMod> Mods;
};

USTRUCT()
struct FBotSTalentTier
{
	GENERATED_BODY()
	UPROPERTY() int32 Level = 1;
	UPROPERTY() TArray<FBotSTalentDef> Options;
};

USTRUCT()
struct FBotSHeroDef
{
	GENERATED_BODY()
	UPROPERTY() FName Id;
	UPROPERTY() FString Name;
	UPROPERTY() EBotSRole Role = EBotSRole::Tank;
	UPROPERTY() FString Concept;
	UPROPERTY() FLinearColor Color = FLinearColor::White;
	/** Merkmal: ShieldOnHit, HealBonusLowHealth, StormAttackSpeed, CritAfterAbility. */
	UPROPERTY() FName TraitType;
	UPROPERTY() FString TraitName;
	UPROPERTY() FString TraitDescription;
	UPROPERTY() TMap<FName, float> TraitParams;
	UPROPERTY() FBotSAbilityDef Basic;
	UPROPERTY() FBotSAbilityDef Ability1;
	UPROPERTY() FBotSAbilityDef Ability2;
	UPROPERTY() FBotSAbilityDef Ability3;
	UPROPERTY() FBotSAbilityDef Ultimate;
	UPROPERTY() TArray<FBotSTalentTier> Talents;

	float Trait(FName Key, float Default = 0.f) const { const float* V = TraitParams.Find(Key); return V ? *V : Default; }
};

USTRUCT()
struct FBotSRoleStats
{
	GENERATED_BODY()
	UPROPERTY() EBotSRole Role = EBotSRole::Tank;
	UPROPERTY() float Health = 1000.f;
	UPROPERTY() float AttackDamage = 50.f;
	UPROPERTY() float AttackSpeed = 1.f;
	UPROPERTY() float AttackRange = 1.5f;
};

USTRUCT()
struct FBotSStormPhase
{
	GENERATED_BODY()
	UPROPERTY() float Wait = 60.f;
	UPROPERTY() float Shrink = 60.f;
	UPROPERTY() float Diameter = 0.f;
	/** Schaden pro Sekunde in Prozent der maximalen Lebenspunkte. */
	UPROPERTY() float DamagePct = 1.f;
};

USTRUCT()
struct FBotSItemDef
{
	GENERATED_BODY()
	UPROPERTY() FName Id;
	UPROPERTY() FString Name;
	UPROPERTY() FString Description;
	UPROPERTY() EBotSItemKind Kind = EBotSItemKind::RelicAttack;
	/** 0 = Gewoehnlich, 1 = Selten. */
	UPROPERTY() int32 Rarity = 0;
	/** Relikt: Heldenwert -> Anteil. */
	UPROPERTY() TMap<FName, float> Stats;
	/** Aktivgegenstand: Faehigkeit, die der Gegenstand gewaehrt. */
	UPROPERTY() FBotSAbilityDef Ability;
	UPROPERTY() int32 Charges = 1;
	UPROPERTY() int32 MaxCharges = 3;
};

USTRUCT()
struct FBotSLootEntry
{
	GENERATED_BODY()
	UPROPERTY() FName Item;
	UPROPERTY() float Weight = 1.f;
};

USTRUCT()
struct FBotSMatchRules
{
	GENERATED_BODY()
	UPROPERTY() int32 MaxPlayers = 8;
	UPROPERTY() int32 SquadSize = 2;
	UPROPERTY() bool bFillWithBots = true;
	UPROPERTY() float WaitTime = 10.f;
	UPROPERTY() float HeroSelectTime = 60.f;
	UPROPERTY() float SpawnSelectTime = 20.f;
	UPROPERTY() float EndScreenTime = 20.f;
	UPROPERTY() float DownedDuration = 30.f;
	UPROPERTY() float DownedHealthPct = 0.3f;
	UPROPERTY() float CrawlSpeed = 1.f;
	UPROPERTY() float ReviveTime = 5.f;
	UPROPERTY() float ReviveHealthPct = 0.3f;
	UPROPERTY() float ReviveRange = 2.5f;
	UPROPERTY() bool bDropItemsOnElimination = true;
	UPROPERTY() int32 MaxLevel = 10;
	UPROPERTY() int32 UltimateLevel = 8;
	UPROPERTY() float LevelScaling = 1.04f;
	/** Erfahrung fuer den Aufstieg von Level i+1 auf i+2. Der letzte Wert gilt fuer alle weiteren Level. */
	UPROPERTY() TArray<float> XpPerLevel;
	UPROPERTY() float CreatureXp = 80.f;
	UPROPERTY() float HeroKillXp = 150.f;
	UPROPERTY() float HeroKillXpPerLevel = 15.f;
	UPROPERTY() float CatchUpPerLevel = 0.15f;
	UPROPERTY() int32 ChestItems = 2;
	UPROPERTY() float CampLootChance = 0.5f;
	UPROPERTY() bool bCreaturesRespawn = false;
};

USTRUCT()
struct FBotSTuning
{
	GENERATED_BODY()
	UPROPERTY() float MoveSpeed = 4.f;
	UPROPERTY() float StormRunSpeedMul = 1.4f;
	UPROPERTY() float StormRunChannel = 1.f;
	UPROPERTY() bool bFaceCursor = true;
	UPROPERTY() float CameraPitch = -60.f;
	UPROPERTY() float CameraDistance = 24.f;
	UPROPERTY() float CameraFOV = 55.f;
	UPROPERTY() float CameraLead = 0.15f;
	UPROPERTY() float CameraLeadMax = 4.f;
	UPROPERTY() float SightRange = 22.f;
	UPROPERTY() float BaseMana = 500.f;
	UPROPERTY() float ManaRegen = 4.f;
	UPROPERTY() float HealthRegen = 4.f;
	UPROPERTY() float InteractRange = 2.5f;
	UPROPERTY() bool bShieldBlocksStorm = false;
	UPROPERTY() float StormStartDiameter = 620.f;
};

USTRUCT()
struct FBotSCreatureDef
{
	GENERATED_BODY()
	UPROPERTY() float Health = 900.f;
	UPROPERTY() float Damage = 55.f;
	UPROPERTY() float AttackInterval = 1.2f;
	UPROPERTY() float AttackRange = 1.8f;
	UPROPERTY() float AggroRange = 8.f;
	UPROPERTY() float LeashRange = 14.f;
	UPROPERTY() float MoveSpeed = 3.6f;
	UPROPERTY() int32 CampSize = 3;
};

USTRUCT()
struct FBotSMapDef
{
	GENERATED_BODY()
	/** Kantenlaenge in Metern. */
	UPROPERTY() float Size = 440.f;
	UPROPERTY() float Cell = 2.f;
	UPROPERTY() int32 Seed = 1337;
	/** Rastermass eines Haeuserblocks in Zellen. */
	UPROPERTY() int32 BlockCells = 20;
	UPROPERTY() int32 BuildingMin = 6;
	UPROPERTY() int32 BuildingMax = 13;
	UPROPERTY() float EmptyBlockChance = 0.3f;
	UPROPERTY() float WallHeight = 3.f;
	UPROPERTY() int32 Chests = 16;
	UPROPERTY() int32 SpawnPoints = 12;
	UPROPERTY() float SpawnRadiusPct = 0.36f;
};

USTRUCT()
struct FBotSGameData
{
	GENERATED_BODY()
	UPROPERTY() FBotSMatchRules MatchRules;
	UPROPERTY() FBotSTuning Tuning;
	UPROPERTY() FBotSCreatureDef Creature;
	UPROPERTY() FBotSMapDef Map;
	UPROPERTY() TArray<FBotSRoleStats> RoleStats;
	UPROPERTY() TArray<FBotSStormPhase> StormPhases;
	UPROPERTY() TArray<FBotSHeroDef> Heroes;
	UPROPERTY() TArray<FBotSItemDef> Items;
	UPROPERTY() TArray<FBotSLootEntry> ChestLoot;
	UPROPERTY() TArray<FBotSLootEntry> CampLoot;

	const FBotSHeroDef* FindHero(FName Id) const { return Heroes.FindByPredicate([Id](const FBotSHeroDef& H) { return H.Id == Id; }); }
	const FBotSItemDef* FindItem(FName Id) const { return Items.FindByPredicate([Id](const FBotSItemDef& I) { return I.Id == Id; }); }
	const FBotSRoleStats& FindRole(EBotSRole Role) const;
};

/** Laedt die Spieldaten einmal aus Content/BotS/Data/GameData.json. */
BATTLEOFTHESTORM_API const FBotSGameData& BotSData();
BATTLEOFTHESTORM_API bool BotSLoadData(FBotSGameData& Out, FString* OutError = nullptr);

// ---------------------------------------------------------------------------------------------
// Sturmzustand, repliziert einmal pro Phase (TDD Abschnitt 9)
// ---------------------------------------------------------------------------------------------
USTRUCT()
struct FBotSStormState
{
	GENERATED_BODY()
	/** 0 = Sturm noch nicht gestartet, 1..n = laufende Phase. */
	UPROPERTY() int32 Phase = 0;
	UPROPERTY() int32 NumPhases = 0;
	UPROPERTY() FVector2D FromCenter = FVector2D::ZeroVector;
	UPROPERTY() float FromRadius = 0.f;
	UPROPERTY() FVector2D ToCenter = FVector2D::ZeroVector;
	UPROPERTY() float ToRadius = 0.f;
	/** Server-Zeit in Sekunden. Alle Laengen in Metern. */
	UPROPERTY() float ShrinkStart = 0.f;
	UPROPERTY() float ShrinkEnd = 0.f;
	UPROPERTY() float DamagePct = 0.f;
};

// ---------------------------------------------------------------------------------------------
// Rechenlogik ohne Weltbezug, durch Automatisierungstests abgedeckt (TDD Abschnitt 16)
// ---------------------------------------------------------------------------------------------
struct BATTLEOFTHESTORM_API FBotSRules
{
	static void StormCircleAt(const FBotSStormState& S, double Time, FVector2D& OutCenter, float& OutRadius);
	/** Waehlt ein Auge mit Radius NewRadius, das vollstaendig im Kreis (Center, Radius) liegt. */
	static FVector2D PickEye(FRandomStream& Rng, FVector2D Center, float Radius, float NewRadius);
	static float LevelFactor(int32 Level, float Base) { return FMath::Pow(Base, float(FMath::Max(1, Level) - 1)); }
	static float XpToNext(const TArray<float>& PerLevel, int32 Level);
	/** Addiert Erfahrung und liefert die Zahl der Aufstiege. */
	static int32 AddXp(int32& Level, float& Xp, float Amount, const TArray<float>& PerLevel, int32 MaxLevel);
	static float HeroKillXp(const FBotSMatchRules& R, int32 VictimLevel, int32 KillerLevel);
	/** Schild vor Lebenspunkten. bBypassShield: Schaden geht am Schild vorbei. */
	static void SplitDamage(float Damage, bool bBypassShield, float& InOutShield, float& InOutHealth);
	static FName RollLoot(const TArray<FBotSLootEntry>& Table, FRandomStream& Rng);
	static float ApplyOp(float Current, FName Op, float Value);
};

// ---------------------------------------------------------------------------------------------
// Kartenlayout: deterministisch aus den Daten erzeugt, identisch auf Server und Clients
// ---------------------------------------------------------------------------------------------
struct BATTLEOFTHESTORM_API FBotSMapLayout
{
	int32 N = 0;
	float Cell = 2.f;
	float Half = 220.f;
	TArray<uint8> Blocked;
	/** Wandquader in Metern (Draufsicht). */
	TArray<FBox2D> Walls;
	TArray<FVector2D> Chests;
	TArray<FVector2D> Camps;
	TArray<FVector2D> Spawns;

	static const FBotSMapLayout& Get();
	void Generate(const FBotSMapDef& Def);

	bool InGrid(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < N && Y < N; }
	bool IsBlocked(int32 X, int32 Y) const { return !InGrid(X, Y) || Blocked[Y * N + X] != 0; }
	FIntPoint ToCell(FVector2D M) const { return FIntPoint(FMath::FloorToInt((M.X + Half) / Cell), FMath::FloorToInt((M.Y + Half) / Cell)); }
	FVector2D CellCenter(FIntPoint C) const { return FVector2D((C.X + 0.5f) * Cell - Half, (C.Y + 0.5f) * Cell - Half); }
	FVector2D NearestFree(FVector2D M) const;
	/** A* auf dem Raster. Liefert Wegpunkte in Metern ohne den Startpunkt. */
	bool FindPath(FVector2D From, FVector2D To, TArray<FVector2D>& OutPath) const;
};

/** Meter (Daten, GDD) nach Unreal-Einheiten. */
inline float BotSM(float Meters) { return Meters * 100.f; }
inline FVector BotSM(FVector2D Meters, float Z = 0.f) { return FVector(Meters.X * 100.f, Meters.Y * 100.f, Z); }
inline FVector2D BotSToM(const FVector& V) { return FVector2D(V.X, V.Y) * 0.01; }
