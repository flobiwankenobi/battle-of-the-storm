#include "Core/BotSTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Rechenlogik laut TDD Abschnitt 16. Start: Automation RunTests BotS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSStormCircleTest, "BotS.Rules.StormCircle", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSStormCircleTest::RunTest(const FString& Parameters)
{
	FBotSStormState S;
	S.Phase = 1;
	S.FromCenter = FVector2D(0.f, 0.f); S.FromRadius = 300.f;
	S.ToCenter = FVector2D(100.f, -50.f); S.ToRadius = 100.f;
	S.ShrinkStart = 120.f; S.ShrinkEnd = 210.f;
	FVector2D C; float R;
	FBotSRules::StormCircleAt(S, 0.0, C, R);
	TestEqual(TEXT("Radius vor dem Schrumpfen"), R, 300.f);
	TestTrue(TEXT("Mitte vor dem Schrumpfen"), C.Equals(S.FromCenter));
	FBotSRules::StormCircleAt(S, 165.0, C, R);
	TestEqual(TEXT("Radius zur Halbzeit"), R, 200.f);
	TestTrue(TEXT("Mitte zur Halbzeit"), C.Equals(FVector2D(50.f, -25.f), 0.01f));
	FBotSRules::StormCircleAt(S, 500.0, C, R);
	TestEqual(TEXT("Radius nach dem Schrumpfen"), R, 100.f);
	TestTrue(TEXT("Mitte nach dem Schrumpfen"), C.Equals(S.ToCenter));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSEyeTest, "BotS.Rules.EyeInsideCircle", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSEyeTest::RunTest(const FString& Parameters)
{
	FRandomStream Rng(42);
	bool bInside = true;
	for (int32 I = 0; I < 2000; ++I)
	{
		const FVector2D Center(Rng.FRandRange(-100.f, 100.f), Rng.FRandRange(-100.f, 100.f));
		const float Radius = Rng.FRandRange(50.f, 310.f), NewRadius = Radius * Rng.FRandRange(0.f, 1.f);
		const FVector2D Eye = FBotSRules::PickEye(Rng, Center, Radius, NewRadius);
		bInside &= FVector2D::Distance(Eye, Center) + NewRadius <= Radius + 0.01f;
	}
	TestTrue(TEXT("Auge liegt vollstaendig im aktuellen Kreis"), bInside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSXpTest, "BotS.Rules.SquadXp", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSXpTest::RunTest(const FString& Parameters)
{
	const TArray<float> PerLevel = { 100.f, 100.f, 150.f };
	int32 Level = 1; float Xp = 0.f;
	TestEqual(TEXT("Kein Aufstieg unter der Schwelle"), FBotSRules::AddXp(Level, Xp, 99.f, PerLevel, 10), 0);
	TestEqual(TEXT("Zwei Aufstiege auf einmal"), FBotSRules::AddXp(Level, Xp, 101.f, PerLevel, 10), 2);
	TestEqual(TEXT("Level"), Level, 3);
	TestEqual(TEXT("Rest"), Xp, 0.f);
	TestEqual(TEXT("Letzter Wert gilt weiter"), FBotSRules::XpToNext(PerLevel, 7), 150.f);
	Level = 9; Xp = 0.f;
	FBotSRules::AddXp(Level, Xp, 10000.f, PerLevel, 10);
	TestEqual(TEXT("Hoechstlevel"), Level, 10);

	FBotSMatchRules Rules;
	Rules.HeroKillXp = 150.f; Rules.HeroKillXpPerLevel = 15.f; Rules.CatchUpPerLevel = 0.15f;
	TestEqual(TEXT("Gleiches Level, kein Bonus"), FBotSRules::HeroKillXp(Rules, 5, 5), 225.f);
	TestEqual(TEXT("Aufholbonus 2 Level"), FBotSRules::HeroKillXp(Rules, 5, 3), 225.f * 1.3f, 0.01f);
	TestEqual(TEXT("Kein Malus gegen schwaechere"), FBotSRules::HeroKillXp(Rules, 3, 5), 195.f);
	TestEqual(TEXT("Level-Skalierung Level 10"), FBotSRules::LevelFactor(10, 1.04f), 1.4233f, 0.001f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSDamageTest, "BotS.Rules.ShieldBeforeHealth", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSDamageTest::RunTest(const FString& Parameters)
{
	float Shield = 100.f, Health = 500.f;
	FBotSRules::SplitDamage(60.f, false, Shield, Health);
	TestEqual(TEXT("Schild faengt ab"), Shield, 40.f);
	TestEqual(TEXT("Lebenspunkte unberuehrt"), Health, 500.f);
	FBotSRules::SplitDamage(100.f, false, Shield, Health);
	TestEqual(TEXT("Schild leer"), Shield, 0.f);
	TestEqual(TEXT("Rest geht auf Lebenspunkte"), Health, 440.f);
	Shield = 100.f;
	FBotSRules::SplitDamage(50.f, true, Shield, Health);
	TestEqual(TEXT("Sturmschaden umgeht den Schild"), Shield, 100.f);
	TestEqual(TEXT("Sturmschaden trifft Lebenspunkte"), Health, 390.f);
	FBotSRules::SplitDamage(9999.f, false, Shield, Health);
	TestEqual(TEXT("Nie unter null"), Health, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSLootTest, "BotS.Rules.WeightedLoot", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSLootTest::RunTest(const FString& Parameters)
{
	TArray<FBotSLootEntry> Table;
	Table.AddDefaulted(3);
	Table[0].Item = "A"; Table[0].Weight = 3.f;
	Table[1].Item = "B"; Table[1].Weight = 1.f;
	Table[2].Item = "Never"; Table[2].Weight = 0.f;
	FRandomStream Rng(7);
	int32 A = 0, B = 0, Never = 0;
	for (int32 I = 0; I < 20000; ++I)
	{
		const FName Item = FBotSRules::RollLoot(Table, Rng);
		if (Item == "A") { ++A; } else if (Item == "B") { ++B; } else { ++Never; }
	}
	TestEqual(TEXT("Gewicht 0 faellt nie"), Never, 0);
	TestTrue(TEXT("Verhaeltnis 3 zu 1"), FMath::IsNearlyEqual(float(A) / float(FMath::Max(1, B)), 3.f, 0.25f));
	TestTrue(TEXT("Leere Tabelle"), FBotSRules::RollLoot(TArray<FBotSLootEntry>(), Rng).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSDataTest, "BotS.Data.GameData", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSDataTest::RunTest(const FString& Parameters)
{
	FBotSGameData Data;
	FString Error;
	const bool bLoaded = BotSLoadData(Data, &Error);
	if (!TestTrue(FString::Printf(TEXT("GameData.json laedt (%s)"), *Error), bLoaded)) { return false; }
	TestEqual(TEXT("4 Helden"), Data.Heroes.Num(), 4);
	TestEqual(TEXT("3 Sturmphasen"), Data.StormPhases.Num(), 3);
	TestEqual(TEXT("6 Rollen"), Data.RoleStats.Num(), 6);
	float StormTime = 0.f;
	for (const FBotSStormPhase& P : Data.StormPhases) { StormTime += P.Wait + P.Shrink; }
	TestEqual(TEXT("Sturm laeuft 465 s"), StormTime, 465.f);
	TestEqual(TEXT("Letzte Phase endet bei Durchmesser 0"), Data.StormPhases.Last().Diameter, 0.f);
	int32 Talents = 0;
	TSet<EBotSRole> Roles;
	for (const FBotSHeroDef& H : Data.Heroes)
	{
		Roles.Add(H.Role);
		for (const FBotSAbilityDef* A : { &H.Basic, &H.Ability1, &H.Ability2, &H.Ability3, &H.Ultimate })
		{
			TestTrue(FString::Printf(TEXT("%s: Faehigkeit vorhanden"), *H.Id.ToString()), A->IsValid());
		}
		// Falsch gelesenes UTF-8 zeigt sich als A mit Tilde (U+00C3).
		TestFalse(FString::Printf(TEXT("%s: Umlaute korrekt gelesen"), *H.Id.ToString()), H.Concept.Contains(TEXT("Ã")) || H.TraitName.Contains(TEXT("Ã")));
		for (const FBotSTalentTier& T : H.Talents)
		{
			Talents += T.Options.Num();
			for (const FBotSTalentDef& O : T.Options)
			{
				for (const FBotSTalentMod& M : O.Mods)
				{
					const bool bKnown = M.Ability.IsNone() || M.Ability == H.Ability1.Id || M.Ability == H.Ability2.Id || M.Ability == H.Ability3.Id || M.Ability == H.Ultimate.Id || M.Ability == H.Basic.Id;
					TestTrue(FString::Printf(TEXT("%s: Talent verweist auf eigene Faehigkeit"), *O.Id.ToString()), bKnown);
				}
			}
		}
	}
	TestEqual(TEXT("4 verschiedene Rollen"), Roles.Num(), 4);
	TestEqual(TEXT("16 Talente"), Talents, 16);
	for (const FBotSLootEntry& E : Data.ChestLoot) { TestNotNull(FString::Printf(TEXT("Beute %s existiert"), *E.Item.ToString()), Data.FindItem(E.Item)); }
	for (const FBotSLootEntry& E : Data.CampLoot) { TestNotNull(FString::Printf(TEXT("Beute %s existiert"), *E.Item.ToString()), Data.FindItem(E.Item)); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBotSMapTest, "BotS.Map.Layout", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FBotSMapTest::RunTest(const FString& Parameters)
{
	FBotSGameData Data;
	if (!TestTrue(TEXT("Daten laden"), BotSLoadData(Data))) { return false; }
	FBotSMapLayout A, B;
	A.Generate(Data.Map);
	B.Generate(Data.Map);
	TestTrue(TEXT("Layout ist deterministisch"), A.Blocked == B.Blocked);
	TestEqual(TEXT("16 Truhen"), A.Chests.Num(), 16);
	TestEqual(TEXT("4 Lager"), A.Camps.Num(), 4);
	TestTrue(TEXT("Mindestens 8 Startpunkte"), A.Spawns.Num() >= 8);
	TArray<FVector2D> Path;
	for (const FVector2D& Spawn : A.Spawns)
	{
		const FIntPoint Cell = A.ToCell(Spawn);
		TestFalse(TEXT("Startpunkt ist frei"), A.IsBlocked(Cell.X, Cell.Y));
		TestTrue(TEXT("Weg vom Startpunkt zur Kartenmitte"), A.FindPath(Spawn, FVector2D::ZeroVector, Path));
	}
	for (const FVector2D& Chest : A.Chests) { TestTrue(TEXT("Weg von der Kartenmitte zur Truhe"), A.FindPath(FVector2D::ZeroVector, Chest, Path)); }
	return true;
}

#endif
