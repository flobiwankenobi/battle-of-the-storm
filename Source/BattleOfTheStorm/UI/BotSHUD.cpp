#include "Core/BotSPlayerController.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Character/BotSCharacters.h"
#include "Match/BotSMatch.h"
#include "Items/BotSItems.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"

// Texte des Prototyps sind deutsch. Umlaute als \u-Folgen, damit die Quelldatei reines ASCII bleibt.
namespace
{
	const FLinearColor ColAlly(0.2f, 0.5f, 1.f);
	const FLinearColor ColSelf(0.2f, 0.9f, 0.3f);
	const FLinearColor ColEnemy(1.f, 0.2f, 0.15f);
	const FLinearColor ColNeutral(1.f, 0.8f, 0.1f);
	const FLinearColor ColPanel(0.02f, 0.02f, 0.03f, 0.75f);
	const FLinearColor ColDim(0.6f, 0.6f, 0.6f);

	FString RoleName(EBotSRole Role)
	{
		switch (Role)
		{
		case EBotSRole::Tank: return TEXT("Tank");
		case EBotSRole::Fighter: return TEXT("Kämpfer");
		case EBotSRole::Healer: return TEXT("Heiler");
		case EBotSRole::Support: return TEXT("Unterstützer");
		case EBotSRole::RangedAssassin: return TEXT("Fernkampf-Assassine");
		default: return TEXT("Nahkampf-Assassine");
		}
	}

	TArray<FString> Wrap(const FString& In, int32 MaxChars)
	{
		TArray<FString> Words, Lines;
		In.ParseIntoArray(Words, TEXT(" "));
		FString Line;
		for (const FString& W : Words)
		{
			if (Line.Len() + W.Len() + 1 > MaxChars && !Line.IsEmpty()) { Lines.Add(Line); Line.Reset(); }
			Line += (Line.IsEmpty() ? TEXT("") : TEXT(" ")) + W;
		}
		if (!Line.IsEmpty()) { Lines.Add(Line); }
		return Lines;
	}

	FString TimeText(float Seconds)
	{
		const int32 S = FMath::Max(0, FMath::CeilToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), S / 60, S % 60);
	}
}

void ABotSHUD::Text(const FString& S, float X, float Y, const FLinearColor& Color, float Scale, bool bCenter)
{
	UFont* Font = GEngine->GetMediumFont();
	const float UiScale = Canvas->ClipY / 1080.f * Scale * 1.35f;
	if (bCenter)
	{
		float W, H;
		GetTextSize(S, W, H, Font, UiScale);
		X -= W * 0.5f;
	}
	DrawText(S, FLinearColor(0.f, 0.f, 0.f, 0.9f), X + 1.f, Y + 1.f, Font, UiScale);
	DrawText(S, Color, X, Y, Font, UiScale);
}

void ABotSHUD::Box(float X, float Y, float W, float H, const FLinearColor& Color) { DrawRect(Color, X, Y, W, H); }

void ABotSHUD::Bar(float X, float Y, float W, float H, float Pct, const FLinearColor& Fill, const FLinearColor& Back)
{
	DrawRect(Back, X, Y, W, H);
	DrawRect(Fill, X, Y, W * FMath::Clamp(Pct, 0.f, 1.f), H);
}

void ABotSHUD::Circle(const FVector2D& Center, float Radius, const FLinearColor& Color, float Thickness)
{
	if (Radius < 1.f) { return; }
	const int32 Segments = 48;
	FVector2D Prev = Center + FVector2D(Radius, 0.f);
	for (int32 I = 1; I <= Segments; ++I)
	{
		const float A = 2.f * PI * I / Segments;
		const FVector2D Cur = Center + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius;
		DrawLine(Prev.X, Prev.Y, Cur.X, Cur.Y, Color, Thickness);
		Prev = Cur;
	}
}

void ABotSHUD::Button(const FString& Label, float X, float Y, float W, float H, FName Id, const FLinearColor& Color)
{
	Box(X, Y, W, H, Color);
	if (!Label.IsEmpty()) { Text(Label, X + W * 0.5f, Y + H * 0.5f - 9.f, FLinearColor::White, 1.f, true); }
	Hits.Add({ FBox2D(FVector2D(X, Y), FVector2D(X + W, Y + H)), Id });
}

bool ABotSHUD::HandleClick(const FVector2D& ScreenPos)
{
	ABotSPlayerController* PC = Cast<ABotSPlayerController>(GetOwningPlayerController());
	for (int32 I = Hits.Num() - 1; PC && I >= 0; --I)
	{
		if (!Hits[I].Rect.IsInside(ScreenPos)) { continue; }
		FString Kind, Value;
		if (!Hits[I].Id.ToString().Split(TEXT(":"), &Kind, &Value)) { return true; }
		if (Kind == TEXT("Hero")) { PC->ServerSelectHero(FName(*Value)); }
		else if (Kind == TEXT("Spawn")) { PC->ServerSelectSpawn(FCString::Atoi(*Value)); }
		else if (Kind == TEXT("Talent"))
		{
			const ABotSPlayerState* PS = PC->GetBotSPlayerState();
			if (PS && PS->GetOpenTalentTier() >= 0) { PC->ServerChooseTalent(PS->GetOpenTalentTier(), FCString::Atoi(*Value)); }
		}
		return true;
	}
	return false;
}

void ABotSHUD::DrawHUD()
{
	Super::DrawHUD();
	Hits.Reset();
	ABotSPlayerController* PC = Cast<ABotSPlayerController>(GetOwningPlayerController());
	ABotSGameState* GS = GetWorld()->GetGameState<ABotSGameState>();
	if (!Canvas || !PC || !GS) { return; }
	ABotSPlayerState* MyPS = PC->GetBotSPlayerState();
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;

	switch (GS->Phase)
	{
	case EBotSMatchPhase::WaitingForPlayers:
		Text(TEXT("Battle of the Storm - Prototyp"), W * 0.5f, H * 0.3f, FLinearColor::White, 2.f, true);
		Text(FString::Printf(TEXT("Warte auf Spieler. Start in %s, freie Plätze werden mit Bots gefüllt."), *TimeText(GS->GetPhaseTimeLeft())), W * 0.5f, H * 0.3f + 60.f * S, ColDim, 1.f, true);
		break;
	case EBotSMatchPhase::HeroSelect:
		DrawHeroSelect(GS, MyPS);
		break;
	case EBotSMatchPhase::SpawnSelect:
		Text(FString::Printf(TEXT("Startpunkt wählen - noch %s"), *TimeText(GS->GetPhaseTimeLeft())), W * 0.5f, 30.f * S, FLinearColor::White, 1.6f, true);
		Text(TEXT("Klick auf einen gelben Punkt. Ohne Wahl gilt der Punkt des Truppführers."), W * 0.5f, 75.f * S, ColDim, 1.f, true);
		DrawMap(GS, MyPS, (W - 820.f * S) * 0.5f, 120.f * S, 820.f * S, true);
		break;
	case EBotSMatchPhase::Match:
	case EBotSMatchPhase::Ended:
		DrawWorldBars(MyPS);
		DrawStormStatus(GS, MyPS);
		if (MyPS)
		{
			DrawSquadFrames(MyPS);
			DrawAbilityBar(MyPS);
			DrawTalentChoice(MyPS);
			DrawPrompts(MyPS);
		}
		DrawMap(GS, MyPS, W - 260.f * S, 20.f * S, 240.f * S, false);
		if (PC->bShowMap) { DrawMap(GS, MyPS, (W - 860.f * S) * 0.5f, (H - 860.f * S) * 0.5f, 860.f * S, false); }
		if (GS->Phase == EBotSMatchPhase::Ended) { DrawMatchEnd(GS, MyPS); }
		break;
	}
}

void ABotSHUD::DrawWorldBars(ABotSPlayerState* MyPS)
{
	const float S = Canvas->ClipY / 1080.f;
	const int32 MyTeam = MyPS ? MyPS->SquadIndex : -2;
	const ABotSGameState* GS = GetWorld()->GetGameState<ABotSGameState>();
	const float Now = GS->GetServerWorldTimeSeconds();
	for (TActorIterator<ABotSCombatCharacter> It(GetWorld()); It; ++It)
	{
		ABotSCombatCharacter* C = *It;
		if (C->IsHidden() || !C->IsTargetable()) { continue; }
		const FVector Screen = Project(C->GetActorLocation() + FVector(0.f, 0.f, 150.f), false);
		if (Screen.Z <= 0.f) { continue; }
		const ABotSHeroCharacter* Hero = Cast<ABotSHeroCharacter>(C);
		const ABotSPlayerState* PS = Hero ? Hero->GetBotSPlayerState() : nullptr;
		const FLinearColor Color = !Hero ? ColNeutral : (PS == MyPS ? ColSelf : (C->GetTeam() == MyTeam ? ColAlly : ColEnemy));
		const float BW = 84.f * S, BH = 9.f * S, X = Screen.X - BW * 0.5f, Y = Screen.Y - 20.f * S;
		const float Max = FMath::Max(1.f, C->GetMaxHealth()), Total = FMath::Max(Max, C->GetHealth() + C->GetShield());
		Box(X - 1.f, Y - 1.f, BW + 2.f, BH + 2.f, FLinearColor::Black);
		Box(X, Y, BW * C->GetHealth() / Total, BH, C->IsDowned() ? ColDim : Color);
		// Schild als heller Abschnitt hinter den Lebenspunkten.
		Box(X + BW * C->GetHealth() / Total, Y, BW * C->GetShield() / Total, BH, FLinearColor(0.9f, 0.9f, 1.f));
		if (PS)
		{
			const FBotSHeroDef* Def = PS->GetHeroDef();
			FString Label = FString::Printf(TEXT("%s (%s)"), *PS->GetPlayerName(), Def ? *Def->Name : TEXT("?"));
			if (C->IsDowned()) { Label = FString::Printf(TEXT("NIEDERGESCHLAGEN %s"), *TimeText(PS->DownedEndTime - Now)); }
			Text(Label, Screen.X, Y - 20.f * S, Color, 0.8f, true);
			if (Hero->ReviveEndTime > 0.f && Hero->ReviveDuration > 0.f)
			{
				Bar(X, Y + BH + 3.f, BW, 5.f * S, 1.f - (Hero->ReviveEndTime - Now) / Hero->ReviveDuration, FLinearColor::White, FLinearColor::Black);
			}
		}
	}
}

void ABotSHUD::DrawAbilityBar(ABotSPlayerState* MyPS)
{
	const FBotSHeroDef* Def = MyPS->GetHeroDef();
	const UBotSAttributeSet* Attr = MyPS->GetAttributes();
	if (!Def || !Attr || !MyPS->IsInMatch()) { return; }
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;
	const float Slot = 78.f * S, Gap = 8.f * S;
	struct FEntry { EBotSSlot Slot; const TCHAR* Key; };
	const FEntry Entries[] = { { EBotSSlot::Basic, TEXT("LMT") }, { EBotSSlot::Ability1, TEXT("1") }, { EBotSSlot::Ability2, TEXT("2") }, { EBotSSlot::Ability3, TEXT("3") },
		{ EBotSSlot::Ultimate, TEXT("4") }, { EBotSSlot::Item1, TEXT("Q") }, { EBotSSlot::Item2, TEXT("E") } };
	const int32 Num = UE_ARRAY_COUNT(Entries);
	const float Total = Num * Slot + (Num - 1) * Gap;
	const float X0 = (W - Total) * 0.5f, Y0 = H - Slot - 24.f * S;

	// Lebenspunkte, Schild und Mana.
	const float Max = FMath::Max(1.f, Attr->GetMaxHealth());
	Bar(X0, Y0 - 48.f * S, Total, 20.f * S, Attr->GetHealth() / Max, MyPS->LifeState == EBotSLifeState::Downed ? ColDim : ColSelf, FLinearColor(0.1f, 0.1f, 0.1f, 0.9f));
	if (Attr->GetShield() > 0.f) { Box(X0, Y0 - 52.f * S, Total * FMath::Min(1.f, Attr->GetShield() / Max), 4.f * S, FLinearColor(0.9f, 0.9f, 1.f)); }
	Text(FString::Printf(TEXT("%.0f / %.0f%s"), Attr->GetHealth(), Attr->GetMaxHealth(), Attr->GetShield() > 0.f ? *FString::Printf(TEXT("  (+%.0f Schild)"), Attr->GetShield()) : TEXT("")), W * 0.5f, Y0 - 49.f * S, FLinearColor::White, 0.8f, true);
	Bar(X0, Y0 - 24.f * S, Total, 12.f * S, Attr->GetMana() / FMath::Max(1.f, Attr->GetMaxMana()), FLinearColor(0.2f, 0.4f, 1.f), FLinearColor(0.1f, 0.1f, 0.1f, 0.9f));
	Text(FString::Printf(TEXT("Merkmal: %s - %s"), *Def->TraitName, *Def->TraitDescription), W * 0.5f, Y0 - 76.f * S, ColDim, 0.75f, true);

	for (int32 I = 0; I < Num; ++I)
	{
		const float X = X0 + I * (Slot + Gap);
		Box(X, Y0, Slot, Slot, ColPanel);
		FBotSAbilityDef Ability;
		const bool bHas = MyPS->GetAbilityDef(Entries[I].Slot, Ability);
		const bool bItem = Entries[I].Slot == EBotSSlot::Item1 || Entries[I].Slot == EBotSSlot::Item2;
		Text(Entries[I].Key, X + 5.f * S, Y0 + 3.f * S, ColNeutral, 0.9f);
		if (!bHas)
		{
			if (bItem) { Text(TEXT("leer"), X + Slot * 0.5f, Y0 + Slot * 0.45f, ColDim, 0.75f, true); }
			continue;
		}
		const bool bLocked = Entries[I].Slot == EBotSSlot::Ultimate && MyPS->GetLevel() < BotSData().MatchRules.UltimateLevel;
		const bool bNoMana = Attr->GetMana() < Ability.ManaCost;
		const TArray<FString> Lines = Wrap(Ability.Name, 10);
		for (int32 L = 0; L < Lines.Num() && L < 2; ++L)
		{
			Text(Lines[L], X + Slot * 0.5f, Y0 + (28.f + L * 16.f) * S, (bLocked || bNoMana) ? ColDim : FLinearColor::White, 0.7f, true);
		}
		if (bItem)
		{
			const int32 Index = Entries[I].Slot == EBotSSlot::Item1 ? 0 : 1;
			Text(FString::Printf(TEXT("x%d"), MyPS->GetInventory()->Actives[Index].Charges), X + Slot - 26.f * S, Y0 + 3.f * S, FLinearColor::White, 0.8f);
		}
		else if (Ability.ManaCost > 0.f)
		{
			Text(FString::Printf(TEXT("%.0f"), Ability.ManaCost), X + Slot - 30.f * S, Y0 + 3.f * S, FLinearColor(0.5f, 0.7f, 1.f), 0.7f);
		}
		float Duration = 0.f;
		const float Remaining = MyPS->GetCooldownRemaining(Entries[I].Slot, Duration);
		if (bLocked)
		{
			Box(X, Y0, Slot, Slot, FLinearColor(0.f, 0.f, 0.f, 0.6f));
			Text(FString::Printf(TEXT("Level %d"), BotSData().MatchRules.UltimateLevel), X + Slot * 0.5f, Y0 + Slot - 22.f * S, ColDim, 0.7f, true);
		}
		else if (Remaining > 0.f && Duration > 0.f)
		{
			const float Pct = Remaining / Duration;
			Box(X, Y0 + Slot * (1.f - Pct), Slot, Slot * Pct, FLinearColor(0.f, 0.f, 0.f, 0.65f));
			if (Entries[I].Slot != EBotSSlot::Basic) { Text(FString::Printf(TEXT("%.1f"), Remaining), X + Slot * 0.5f, Y0 + Slot - 24.f * S, FLinearColor::White, 1.f, true); }
		}
	}

	// Relikte rechts neben der Leiste.
	float RY = Y0;
	for (const FBotSItemSlot& Relic : MyPS->GetInventory()->Relics)
	{
		const FBotSItemDef* Item = Relic.Item.IsNone() ? nullptr : BotSData().FindItem(Relic.Item);
		if (Item) { Text(FString::Printf(TEXT("%s: %s"), *Item->Name, *Item->Description), X0 + Total + 20.f * S, RY, Item->Rarity > 0 ? FLinearColor(0.4f, 0.6f, 1.f) : FLinearColor(0.4f, 0.9f, 0.4f), 0.7f); RY += 20.f * S; }
	}
}

void ABotSHUD::DrawSquadFrames(ABotSPlayerState* MyPS)
{
	const ABotSSquadState* Squad = MyPS->Squad;
	if (!Squad) { return; }
	const float H = Canvas->ClipY, S = H / 1080.f;
	const FBotSMatchRules& Rules = BotSData().MatchRules;
	float Y = H - 60.f * S - Squad->Members.Num() * 46.f * S;
	const float X = 24.f * S;
	const bool bMax = Squad->Level >= Rules.MaxLevel;
	Text(FString::Printf(TEXT("Trupp %d - Level %d"), Squad->SquadIndex + 1, Squad->Level), X, Y - 44.f * S, FLinearColor::White, 1.1f);
	Bar(X, Y - 16.f * S, 220.f * S, 6.f * S, bMax ? 1.f : Squad->Xp / FBotSRules::XpToNext(Rules.XpPerLevel, Squad->Level), FLinearColor(0.8f, 0.5f, 1.f), FLinearColor(0.1f, 0.1f, 0.1f, 0.9f));
	for (const ABotSPlayerState* PS : Squad->Members)
	{
		if (!PS) { continue; }
		const FBotSHeroDef* Def = PS->GetHeroDef();
		const UBotSAttributeSet* Attr = PS->GetAttributes();
		FString Status;
		if (PS->LifeState == EBotSLifeState::Downed) { Status = TEXT(" - niedergeschlagen"); }
		else if (PS->LifeState == EBotSLifeState::Eliminated) { Status = TEXT(" - ausgeschaltet"); }
		Text(FString::Printf(TEXT("%s (%s)%s"), *PS->GetPlayerName(), Def ? *Def->Name : TEXT("?"), *Status), X, Y, PS == MyPS ? ColSelf : ColAlly, 0.8f);
		Bar(X, Y + 22.f * S, 220.f * S, 10.f * S, PS->IsInMatch() ? Attr->GetHealth() / FMath::Max(1.f, Attr->GetMaxHealth()) : 0.f, PS->IsAlive() ? ColSelf : ColDim, FLinearColor(0.1f, 0.1f, 0.1f, 0.9f));
		Y += 46.f * S;
	}
}

void ABotSHUD::DrawStormStatus(ABotSGameState* GS, ABotSPlayerState* MyPS)
{
	const float W = Canvas->ClipX, S = Canvas->ClipY / 1080.f;
	const FBotSStormState& Storm = GS->Storm;
	const float Now = GS->GetServerWorldTimeSeconds();
	FString Line;
	if (Storm.Phase <= 0) { Line = TEXT("Sturm ruht"); }
	else if (Now < Storm.ShrinkStart) { Line = FString::Printf(TEXT("Phase %d/%d - Sturm schrumpft in %s"), Storm.Phase, Storm.NumPhases, *TimeText(Storm.ShrinkStart - Now)); }
	else if (Now < Storm.ShrinkEnd) { Line = FString::Printf(TEXT("Phase %d/%d - Sturm schrumpft, noch %s"), Storm.Phase, Storm.NumPhases, *TimeText(Storm.ShrinkEnd - Now)); }
	else { Line = FString::Printf(TEXT("Phase %d/%d - Sturm geschlossen"), Storm.Phase, Storm.NumPhases); }
	Box(W * 0.5f - 260.f * S, 12.f * S, 520.f * S, 62.f * S, ColPanel);
	Text(Line, W * 0.5f, 16.f * S, FLinearColor::White, 1.1f, true);
	Text(FString::Printf(TEXT("Trupps: %d   Spieler: %d"), GS->NumSquadsAlive(), GS->NumPlayersAlive()), W * 0.5f, 46.f * S, ColDim, 0.9f, true);
	if (MyPS && MyPS->bInStorm && MyPS->IsInMatch())
	{
		Text(FString::Printf(TEXT("IM STURM - %.0f %% Lebenspunkte pro Sekunde"), Storm.DamagePct), W * 0.5f, 110.f * S, ColEnemy, 1.6f, true);
	}
}

void ABotSHUD::DrawMap(ABotSGameState* GS, ABotSPlayerState* MyPS, float X, float Y, float Size, bool bSpawnSelect)
{
	const FBotSMapLayout& Layout = FBotSMapLayout::Get();
	const float Scale = Size / (Layout.Half * 2.f);
	// Bildschirm oben entspricht +X der Welt, Bildschirm rechts +Y.
	auto ToScreen = [&](FVector2D M) { return FVector2D(X + (M.Y + Layout.Half) * Scale, Y + (Layout.Half - M.X) * Scale); };
	Box(X - 2.f, Y - 2.f, Size + 4.f, Size + 4.f, FLinearColor::Black);
	Box(X, Y, Size, Size, FLinearColor(0.1f, 0.12f, 0.1f, 0.92f));
	for (const FBox2D& Wall : Layout.Walls)
	{
		const FVector2D A = ToScreen(FVector2D(Wall.Max.X, Wall.Min.Y)), B = ToScreen(FVector2D(Wall.Min.X, Wall.Max.Y));
		Box(A.X, A.Y, FMath::Max(1.f, B.X - A.X), FMath::Max(1.f, B.Y - A.Y), FLinearColor(0.45f, 0.42f, 0.38f));
	}

	if (GS->Phase >= EBotSMatchPhase::Match)
	{
		FVector2D Center; float Radius;
		GS->GetStormCircle(Center, Radius);
		Circle(ToScreen(Center), Radius * Scale, FLinearColor(0.7f, 0.3f, 1.f), 2.f);
		if (GS->Storm.Phase > 0) { Circle(ToScreen(GS->Storm.ToCenter), GS->Storm.ToRadius * Scale, FLinearColor::White, 1.f); }
	}

	if (bSpawnSelect)
	{
		const ABotSPlayerState* Leader = (MyPS && MyPS->Squad && MyPS->Squad->Members.Num() > 0) ? MyPS->Squad->Members[0].Get() : nullptr;
		for (int32 I = 0; I < Layout.Spawns.Num(); ++I)
		{
			const FVector2D P = ToScreen(Layout.Spawns[I]);
			const bool bMine = MyPS && MyPS->SpawnChoice == I;
			const bool bLeader = Leader && Leader != MyPS && Leader->SpawnChoice == I;
			const float R = 13.f * Canvas->ClipY / 1080.f;
			Button(FString(), P.X - R, P.Y - R, R * 2.f, R * 2.f, FName(*FString::Printf(TEXT("Spawn:%d"), I)), bMine ? ColSelf : (bLeader ? ColAlly : ColNeutral));
		}
		return;
	}

	if (MyPS && MyPS->Squad)
	{
		for (const ABotSPlayerState* PS : MyPS->Squad->Members)
		{
			const ABotSHeroCharacter* Hero = PS ? PS->GetHero() : nullptr;
			if (!Hero) { continue; }
			const FVector2D P = ToScreen(BotSToM(Hero->GetActorLocation()));
			Box(P.X - 4.f, P.Y - 4.f, 8.f, 8.f, PS == MyPS ? ColSelf : ColAlly);
		}
	}
}

void ABotSHUD::DrawTalentChoice(ABotSPlayerState* MyPS)
{
	const int32 Tier = MyPS->GetOpenTalentTier();
	const FBotSHeroDef* Def = MyPS->GetHeroDef();
	if (Tier < 0 || !Def || !Def->Talents.IsValidIndex(Tier)) { return; }
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;
	const TArray<FBotSTalentDef>& Options = Def->Talents[Tier].Options;
	const float CardW = 330.f * S, CardH = 130.f * S, Gap = 20.f * S;
	const float X0 = (W - (Options.Num() * CardW + (Options.Num() - 1) * Gap)) * 0.5f, Y0 = H * 0.62f;
	Text(FString::Printf(TEXT("Talent wählen (Level %d)"), Def->Talents[Tier].Level), W * 0.5f, Y0 - 34.f * S, ColNeutral, 1.1f, true);
	for (int32 I = 0; I < Options.Num(); ++I)
	{
		const float X = X0 + I * (CardW + Gap);
		Button(FString(), X, Y0, CardW, CardH, FName(*FString::Printf(TEXT("Talent:%d"), I)), FLinearColor(0.08f, 0.08f, 0.14f, 0.9f));
		Text(FString::Printf(TEXT("Strg+%d  %s"), I + 1, *Options[I].Name), X + 12.f * S, Y0 + 8.f * S, ColNeutral, 1.f);
		float LY = Y0 + 40.f * S;
		for (const FString& Line : Wrap(Options[I].Description, 40)) { Text(Line, X + 12.f * S, LY, FLinearColor::White, 0.8f); LY += 22.f * S; }
	}
}

void ABotSHUD::DrawHeroSelect(ABotSGameState* GS, ABotSPlayerState* MyPS)
{
	const FBotSGameData& Data = BotSData();
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;
	Text(FString::Printf(TEXT("Heldenwahl - noch %s"), *TimeText(GS->GetPhaseTimeLeft())), W * 0.5f, 40.f * S, FLinearColor::White, 1.8f, true);
	const float CardW = 400.f * S, CardH = 560.f * S, Gap = 24.f * S;
	const float X0 = (W - (Data.Heroes.Num() * CardW + (Data.Heroes.Num() - 1) * Gap)) * 0.5f, Y0 = 130.f * S;
	TSet<EBotSRole> Roles;
	for (int32 I = 0; I < Data.Heroes.Num(); ++I)
	{
		const FBotSHeroDef& Hero = Data.Heroes[I];
		const float X = X0 + I * (CardW + Gap);
		bool bMine = MyPS && MyPS->HeroId == Hero.Id, bTaken = false;
		if (MyPS && MyPS->Squad)
		{
			for (const ABotSPlayerState* Mate : MyPS->Squad->Members) { if (Mate && Mate != MyPS && Mate->HeroId == Hero.Id) { bTaken = true; } }
		}
		if (bMine || bTaken) { Roles.Add(Hero.Role); }
		Button(FString(), X, Y0, CardW, CardH, FName(*FString::Printf(TEXT("Hero:%s"), *Hero.Id.ToString())), bMine ? FLinearColor(0.1f, 0.3f, 0.12f, 0.95f) : (bTaken ? FLinearColor(0.2f, 0.2f, 0.2f, 0.9f) : FLinearColor(0.08f, 0.08f, 0.14f, 0.92f)));
		Box(X, Y0, CardW, 8.f * S, Hero.Color);
		Text(Hero.Name, X + 16.f * S, Y0 + 18.f * S, FLinearColor::White, 1.6f);
		Text(RoleName(Hero.Role) + (bTaken ? TEXT("  (im Trupp vergeben)") : (bMine ? TEXT("  (gewählt)") : TEXT(""))), X + 16.f * S, Y0 + 62.f * S, ColNeutral, 0.9f);
		float LY = Y0 + 96.f * S;
		auto Lines = [&](const FString& Title, const FString& Body, const FLinearColor& Color)
		{
			Text(Title, X + 16.f * S, LY, Color, 0.85f); LY += 22.f * S;
			for (const FString& Line : Wrap(Body, 46)) { Text(Line, X + 16.f * S, LY, ColDim, 0.72f); LY += 19.f * S; }
			LY += 6.f * S;
		};
		Lines(Hero.Concept, FString(), FLinearColor::White);
		Lines(TEXT("Merkmal: ") + Hero.TraitName, Hero.TraitDescription, ColNeutral);
		Lines(TEXT("1: ") + Hero.Ability1.Name, Hero.Ability1.Description, FLinearColor::White);
		Lines(TEXT("2: ") + Hero.Ability2.Name, Hero.Ability2.Description, FLinearColor::White);
		Lines(TEXT("3: ") + Hero.Ability3.Name, Hero.Ability3.Description, FLinearColor::White);
		Lines(FString::Printf(TEXT("4 (Level %d): "), Data.MatchRules.UltimateLevel) + Hero.Ultimate.Name, Hero.Ultimate.Description, FLinearColor::White);
	}

	float Y = Y0 + CardH + 30.f * S;
	if (MyPS && MyPS->Squad)
	{
		FString Line = FString::Printf(TEXT("Trupp %d: "), MyPS->Squad->SquadIndex + 1);
		for (const ABotSPlayerState* Mate : MyPS->Squad->Members)
		{
			if (!Mate) { continue; }
			const FBotSHeroDef* Def = Mate->GetHeroDef();
			Line += FString::Printf(TEXT("%s [%s]   "), *Mate->GetPlayerName(), Def ? *Def->Name : TEXT("wählt noch"));
		}
		Text(Line, W * 0.5f, Y, ColAlly, 1.f, true);
		FString Missing;
		for (const FBotSHeroDef& Hero : Data.Heroes) { if (!Roles.Contains(Hero.Role)) { Missing += RoleName(Hero.Role) + TEXT("  "); } }
		if (!Missing.IsEmpty()) { Text(TEXT("Fehlende Rollen im Trupp: ") + Missing, W * 0.5f, Y + 30.f * S, ColDim, 0.9f, true); }
	}
	else
	{
		Text(TEXT("Kein freier Platz in einem Trupp - du schaust zu."), W * 0.5f, Y, ColDim, 1.f, true);
	}
}

void ABotSHUD::DrawMatchEnd(ABotSGameState* GS, ABotSPlayerState* MyPS)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;
	Box(0.f, H * 0.28f, W, 230.f * S, FLinearColor(0.f, 0.f, 0.f, 0.8f));
	const ABotSSquadState* Squad = MyPS ? MyPS->Squad.Get() : nullptr;
	const bool bWon = Squad && Squad->SquadIndex == GS->WinnerSquad;
	FString Title = TEXT("Match beendet");
	if (bWon) { Title = TEXT("SIEG"); }
	else if (Squad && Squad->Placement > 0) { Title = FString::Printf(TEXT("Platz %d"), Squad->Placement); }
	Text(Title, W * 0.5f, H * 0.28f + 30.f * S, bWon ? ColNeutral : FLinearColor::White, 3.5f, true);
	FString Winners;
	if (GS->Squads.IsValidIndex(GS->WinnerSquad) && GS->Squads[GS->WinnerSquad])
	{
		for (const ABotSPlayerState* PS : GS->Squads[GS->WinnerSquad]->Members) { if (PS) { Winners += PS->GetPlayerName() + TEXT("  "); } }
	}
	Text(FString::Printf(TEXT("Sieger: Trupp %d  -  %s"), GS->WinnerSquad + 1, *Winners), W * 0.5f, H * 0.28f + 150.f * S, ColDim, 1.2f, true);
}

void ABotSHUD::DrawPrompts(ABotSPlayerState* MyPS)
{
	const float W = Canvas->ClipX, H = Canvas->ClipY, S = H / 1080.f;
	const ABotSGameState* GS = GetWorld()->GetGameState<ABotSGameState>();
	const float Now = GS->GetServerWorldTimeSeconds();
	const float Y = H * 0.72f;
	if (MyPS->LifeState == EBotSLifeState::Eliminated) { Text(TEXT("Ausgeschaltet - du schaust deinem Trupp zu"), W * 0.5f, Y, ColDim, 1.4f, true); return; }
	if (MyPS->LifeState == EBotSLifeState::Downed) { Text(FString::Printf(TEXT("Niedergeschlagen - Ausbluten in %s"), *TimeText(MyPS->DownedEndTime - Now)), W * 0.5f, Y, ColEnemy, 1.4f, true); return; }
	const ABotSHeroCharacter* Hero = MyPS->GetHero();
	if (!Hero) { return; }
	if (Hero->IsLocalStormRunPending() || Hero->StormRunChannelEnd > 0.f) { Text(TEXT("Sturmlauf wird kanalisiert ..."), W * 0.5f, Y, FLinearColor::White, 1.f, true); return; }
	if (Hero->bStormRunAllowed) { Text(TEXT("Sturmlauf aktiv"), W * 0.5f, Y, ColAlly, 1.f, true); }
	if (GS->Phase != EBotSMatchPhase::Match) { return; }

	const FVector Loc = Hero->GetActorLocation();
	const float Range = BotSM(BotSData().Tuning.InteractRange), ReviveRange = BotSM(BotSData().MatchRules.ReviveRange);
	FString Prompt;
	for (TActorIterator<ABotSHeroCharacter> It(GetWorld()); It && Prompt.IsEmpty(); ++It)
	{
		if (*It != Hero && It->IsDowned() && It->GetTeam() == Hero->GetTeam() && FVector::DistSquared2D(It->GetActorLocation(), Loc) <= ReviveRange * ReviveRange) { Prompt = TEXT("[F] halten: Wiederbeleben"); }
	}
	for (TActorIterator<ABotSPickup> It(GetWorld()); It && Prompt.IsEmpty(); ++It)
	{
		const FBotSItemDef* Item = BotSData().FindItem(It->ItemId);
		if (Item && FVector::DistSquared2D(It->GetActorLocation(), Loc) <= Range * Range) { Prompt = FString::Printf(TEXT("[F] Aufheben: %s (%s)"), *Item->Name, *Item->Description); }
	}
	for (TActorIterator<ABotSLootChest> It(GetWorld()); It && Prompt.IsEmpty(); ++It)
	{
		if (!It->bOpened && FVector::DistSquared2D(It->GetActorLocation(), Loc) <= Range * Range) { Prompt = TEXT("[F] Truhe öffnen"); }
	}
	if (!Prompt.IsEmpty()) { Text(Prompt, W * 0.5f, Y + 34.f * S, ColNeutral, 1.1f, true); }
}
