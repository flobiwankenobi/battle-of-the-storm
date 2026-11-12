#include "Core/BotSTypes.h"
#include "BattleOfTheStorm.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UE_DEFINE_GAMEPLAY_TAG(TAG_State_Downed, "State.Downed");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_InStorm, "State.InStorm");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Channeling, "State.Channeling");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Stunned, "State.Stunned");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Rooted, "State.Rooted");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_StormRun, "State.StormRun");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Invulnerable, "State.Invulnerable");
UE_DEFINE_GAMEPLAY_TAG(TAG_Damage_Storm, "Damage.Storm");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_Damage, "Data.Damage");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_Heal, "Data.Heal");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_Mana, "Data.Mana");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_Duration, "Data.Duration");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_MoveSpeedMul, "Data.MoveSpeedMul");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_AttackSpeedMul, "Data.AttackSpeedMul");

// ---------------------------------------------------------------------------------------------
// Daten
// ---------------------------------------------------------------------------------------------
const FBotSRoleStats& FBotSGameData::FindRole(EBotSRole Role) const
{
	static const FBotSRoleStats Fallback;
	const FBotSRoleStats* R = RoleStats.FindByPredicate([Role](const FBotSRoleStats& S) { return S.Role == Role; });
	return R ? *R : Fallback;
}

bool BotSLoadData(FBotSGameData& Out, FString* OutError)
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("BotS/Data/GameData.json");
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		if (OutError) { *OutError = FString::Printf(TEXT("Datei fehlt: %s"), *Path); }
		return false;
	}
	FText Reason;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Out, 0, 0, false, &Reason))
	{
		if (OutError) { *OutError = Reason.ToString(); }
		return false;
	}
	return true;
}

const FBotSGameData& BotSData()
{
	static FBotSGameData Data;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Error;
		if (BotSLoadData(Data, &Error))
		{
			UE_LOG(LogBotS, Log, TEXT("Spieldaten geladen: %d Helden, %d Gegenstaende, %d Sturmphasen"), Data.Heroes.Num(), Data.Items.Num(), Data.StormPhases.Num());
		}
		else
		{
			UE_LOG(LogBotS, Error, TEXT("Spieldaten nicht geladen: %s"), *Error);
		}
	}
	return Data;
}

// ---------------------------------------------------------------------------------------------
// Rechenlogik
// ---------------------------------------------------------------------------------------------
void FBotSRules::StormCircleAt(const FBotSStormState& S, double Time, FVector2D& OutCenter, float& OutRadius)
{
	float Alpha = 0.f;
	if (S.ShrinkEnd > S.ShrinkStart)
	{
		Alpha = FMath::Clamp(float((Time - S.ShrinkStart) / (S.ShrinkEnd - S.ShrinkStart)), 0.f, 1.f);
	}
	else if (Time >= S.ShrinkEnd)
	{
		Alpha = 1.f;
	}
	OutCenter = FMath::Lerp(S.FromCenter, S.ToCenter, Alpha);
	OutRadius = FMath::Lerp(S.FromRadius, S.ToRadius, Alpha);
}

FVector2D FBotSRules::PickEye(FRandomStream& Rng, FVector2D Center, float Radius, float NewRadius)
{
	const float MaxOffset = FMath::Max(0.f, Radius - NewRadius);
	const float Angle = Rng.FRandRange(0.f, 2.f * PI);
	// Wurzel: gleichmaessige Verteilung ueber die Kreisflaeche.
	const float Dist = MaxOffset * FMath::Sqrt(Rng.FRand());
	return Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Dist;
}

float FBotSRules::XpToNext(const TArray<float>& PerLevel, int32 Level)
{
	if (PerLevel.Num() == 0) { return 100.f; }
	return PerLevel[FMath::Clamp(Level - 1, 0, PerLevel.Num() - 1)];
}

int32 FBotSRules::AddXp(int32& Level, float& Xp, float Amount, const TArray<float>& PerLevel, int32 MaxLevel)
{
	int32 Ups = 0;
	Xp += FMath::Max(0.f, Amount);
	while (Level < MaxLevel)
	{
		const float Need = XpToNext(PerLevel, Level);
		if (Xp < Need) { break; }
		Xp -= Need;
		++Level;
		++Ups;
	}
	if (Level >= MaxLevel) { Xp = 0.f; }
	return Ups;
}

float FBotSRules::HeroKillXp(const FBotSMatchRules& R, int32 VictimLevel, int32 KillerLevel)
{
	const float Base = R.HeroKillXp + R.HeroKillXpPerLevel * VictimLevel;
	const int32 Diff = FMath::Max(0, VictimLevel - KillerLevel);
	return Base * (1.f + R.CatchUpPerLevel * Diff);
}

void FBotSRules::SplitDamage(float Damage, bool bBypassShield, float& InOutShield, float& InOutHealth)
{
	Damage = FMath::Max(0.f, Damage);
	const float Absorbed = bBypassShield ? 0.f : FMath::Min(InOutShield, Damage);
	InOutShield -= Absorbed;
	InOutHealth = FMath::Max(0.f, InOutHealth - (Damage - Absorbed));
}

FName FBotSRules::RollLoot(const TArray<FBotSLootEntry>& Table, FRandomStream& Rng)
{
	float Total = 0.f;
	for (const FBotSLootEntry& E : Table) { Total += FMath::Max(0.f, E.Weight); }
	if (Total <= 0.f) { return NAME_None; }
	float Roll = Rng.FRand() * Total;
	for (const FBotSLootEntry& E : Table)
	{
		Roll -= FMath::Max(0.f, E.Weight);
		if (Roll < 0.f) { return E.Item; }
	}
	return Table.Last().Item;
}

float FBotSRules::ApplyOp(float Current, FName Op, float Value)
{
	if (Op == "Set") { return Value; }
	if (Op == "Mul") { return Current * Value; }
	return Current + Value;
}

// ---------------------------------------------------------------------------------------------
// Kartenlayout
// ---------------------------------------------------------------------------------------------
const FBotSMapLayout& FBotSMapLayout::Get()
{
	static FBotSMapLayout Layout;
	if (Layout.N == 0) { Layout.Generate(BotSData().Map); }
	return Layout;
}

void FBotSMapLayout::Generate(const FBotSMapDef& Def)
{
	Cell = FMath::Max(0.5f, Def.Cell);
	N = FMath::Max(20, FMath::RoundToInt(Def.Size / Cell));
	Half = N * Cell * 0.5f;
	Blocked.Init(0, N * N);
	Walls.Reset(); Chests.Reset(); Camps.Reset(); Spawns.Reset();

	FRandomStream Rng(Def.Seed);
	auto Block = [this](int32 X, int32 Y) { if (InGrid(X, Y)) { Blocked[Y * N + X] = 1; } };

	// Rand der Karte.
	for (int32 I = 0; I < N; ++I) { Block(I, 0); Block(I, N - 1); Block(0, I); Block(N - 1, I); }

	const int32 BC = FMath::Max(8, Def.BlockCells);
	const int32 NumBlocks = N / BC;
	const int32 Mid = NumBlocks / 2;
	const int32 Q1 = FMath::Max(1, NumBlocks / 4), Q3 = NumBlocks - 1 - Q1;
	const TArray<FIntPoint> CampBlocks = { {Q1, Q1}, {Q3, Q1}, {Q1, Q3}, {Q3, Q3} };
	TArray<FVector2D> BuildingCenters;

	for (int32 By = 0; By < NumBlocks; ++By)
	{
		for (int32 Bx = 0; Bx < NumBlocks; ++Bx)
		{
			const FIntPoint BlockCenter(Bx * BC + BC / 2, By * BC + BC / 2);
			const float EmptyRoll = Rng.FRand();
			if (CampBlocks.Contains(FIntPoint(Bx, By)))
			{
				Camps.Add(CellCenter(BlockCenter));
				continue;
			}
			if ((Bx == Mid && By == Mid) || EmptyRoll < Def.EmptyBlockChance) { continue; }

			const int32 MaxSize = FMath::Min(Def.BuildingMax, BC - 6);
			const int32 W = Rng.RandRange(Def.BuildingMin, MaxSize);
			const int32 H = Rng.RandRange(Def.BuildingMin, MaxSize);
			const int32 X0 = BlockCenter.X - W / 2, Y0 = BlockCenter.Y - H / 2;
			const int32 X1 = X0 + W - 1, Y1 = Y0 + H - 1;
			// Je Seite eine Luecke von 2 Zellen (Tuer oder Bresche).
			const int32 GapTop = Rng.RandRange(X0 + 1, X1 - 2), GapBottom = Rng.RandRange(X0 + 1, X1 - 2);
			const int32 GapLeft = Rng.RandRange(Y0 + 1, Y1 - 2), GapRight = Rng.RandRange(Y0 + 1, Y1 - 2);
			for (int32 X = X0; X <= X1; ++X)
			{
				if (X != GapTop && X != GapTop + 1) { Block(X, Y0); }
				if (X != GapBottom && X != GapBottom + 1) { Block(X, Y1); }
			}
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				if (Y != GapLeft && Y != GapLeft + 1) { Block(X0, Y); }
				if (Y != GapRight && Y != GapRight + 1) { Block(X1, Y); }
			}
			BuildingCenters.Add(FVector2D((X0 + X1 + 1) * 0.5f * Cell - Half, (Y0 + Y1 + 1) * 0.5f * Cell - Half));
		}
	}

	// Truhen in zufaellig gewaehlten Gebaeuden.
	for (int32 I = BuildingCenters.Num() - 1; I > 0; --I) { BuildingCenters.Swap(I, Rng.RandRange(0, I)); }
	for (int32 I = 0; I < FMath::Min(Def.Chests, BuildingCenters.Num()); ++I) { Chests.Add(BuildingCenters[I]); }

	// Zusammenhaengende Zellen einer Zeile zu einem Quader zusammenfassen.
	for (int32 Y = 0; Y < N; ++Y)
	{
		int32 X = 0;
		while (X < N)
		{
			if (!Blocked[Y * N + X]) { ++X; continue; }
			const int32 Start = X;
			while (X < N && Blocked[Y * N + X]) { ++X; }
			Walls.Add(FBox2D(FVector2D(Start * Cell - Half, Y * Cell - Half), FVector2D(X * Cell - Half, (Y + 1) * Cell - Half)));
		}
	}

	// Startpunkte auf einem Ring.
	for (int32 I = 0; I < Def.SpawnPoints; ++I)
	{
		const float Angle = 2.f * PI * I / FMath::Max(1, Def.SpawnPoints);
		Spawns.Add(NearestFree(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Def.Size * Def.SpawnRadiusPct));
	}
}

FVector2D FBotSMapLayout::NearestFree(FVector2D M) const
{
	const FIntPoint C = ToCell(M);
	for (int32 R = 0; R < 12; ++R)
	{
		for (int32 Dy = -R; Dy <= R; ++Dy)
		{
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != R) { continue; }
				if (!IsBlocked(C.X + Dx, C.Y + Dy)) { return R == 0 ? M : CellCenter(FIntPoint(C.X + Dx, C.Y + Dy)); }
			}
		}
	}
	return M;
}

bool FBotSMapLayout::FindPath(FVector2D From, FVector2D To, TArray<FVector2D>& OutPath) const
{
	OutPath.Reset();
	const FIntPoint S = ToCell(NearestFree(From)), G = ToCell(NearestFree(To));
	if (IsBlocked(S.X, S.Y) || IsBlocked(G.X, G.Y)) { return false; }
	if (S == G) { OutPath.Add(To); return true; }

	struct FNode { float F; int32 Index; };
	auto Less = [](const FNode& A, const FNode& B) { return A.F < B.F; };
	auto Heuristic = [&G, this](int32 Index)
	{
		const int32 Dx = FMath::Abs(Index % N - G.X), Dy = FMath::Abs(Index / N - G.Y);
		return float(FMath::Max(Dx, Dy)) + 0.4142f * FMath::Min(Dx, Dy);
	};

	TArray<float> Cost; Cost.Init(TNumericLimits<float>::Max(), N * N);
	TArray<int32> Parent; Parent.Init(-1, N * N);
	TArray<FNode> Open;
	const int32 Start = S.Y * N + S.X, Goal = G.Y * N + G.X;
	Cost[Start] = 0.f;
	Open.HeapPush(FNode{ Heuristic(Start), Start }, Less);

	bool bFound = false;
	while (Open.Num() > 0)
	{
		FNode Cur;
		Open.HeapPop(Cur, Less, EAllowShrinking::No);
		if (Cur.Index == Goal) { bFound = true; break; }
		if (Cur.F > Cost[Cur.Index] + Heuristic(Cur.Index) + KINDA_SMALL_NUMBER) { continue; }
		const int32 Cx = Cur.Index % N, Cy = Cur.Index / N;
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				if (Dx == 0 && Dy == 0) { continue; }
				const int32 Nx = Cx + Dx, Ny = Cy + Dy;
				if (IsBlocked(Nx, Ny)) { continue; }
				// Nicht diagonal um Ecken schneiden.
				if (Dx != 0 && Dy != 0 && (IsBlocked(Cx + Dx, Cy) || IsBlocked(Cx, Cy + Dy))) { continue; }
				const int32 Ni = Ny * N + Nx;
				const float NewCost = Cost[Cur.Index] + ((Dx != 0 && Dy != 0) ? 1.4142f : 1.f);
				if (NewCost < Cost[Ni])
				{
					Cost[Ni] = NewCost;
					Parent[Ni] = Cur.Index;
					Open.HeapPush(FNode{ NewCost + Heuristic(Ni), Ni }, Less);
				}
			}
		}
	}
	if (!bFound) { return false; }

	TArray<FVector2D> Reverse;
	for (int32 I = Goal; I != Start && I >= 0; I = Parent[I]) { Reverse.Add(CellCenter(FIntPoint(I % N, I / N))); }
	for (int32 I = Reverse.Num() - 1; I >= 0; --I) { OutPath.Add(Reverse[I]); }
	if (OutPath.Num() > 0) { OutPath.Last() = NearestFree(To); }
	return true;
}
