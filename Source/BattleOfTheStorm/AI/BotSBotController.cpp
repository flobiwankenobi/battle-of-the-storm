#include "Core/BotSPlayerController.h"
#include "BattleOfTheStorm.h"
#include "Abilities/BotSAbilities.h"
#include "Character/BotSCharacters.h"
#include "Match/BotSMatch.h"
#include "Items/BotSItems.h"
#include "EngineUtils.h"
#include "Engine/World.h"

ABotSBotController::ABotSBotController()
{
	bWantsPlayerState = true;
	PrimaryActorTick.bCanEverTick = true;
}

bool ABotSBotController::HasLineOfSight(const FVector& From, const FVector& To) const
{
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(BotSBotSight), false);
	return !GetWorld()->LineTraceTestByChannel(FVector(From.X, From.Y, 60.f), FVector(To.X, To.Y, 60.f), ECC_Visibility, Query);
}

void ABotSBotController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ABotSHeroCharacter* Hero = Cast<ABotSHeroCharacter>(GetPawn());
	ABotSPlayerState* PS = GetPlayerState<ABotSPlayerState>();
	if (!Hero || !PS) { return; }
	// Bots haben keinen Client: der Wunsch steht dauerhaft, die Freigabe des Servers entscheidet.
	Hero->GetBotSMovement()->bWantsStormRun = true;
	if (!PS->IsAlive())
	{
		Hero->SetAttacking(false);
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextThink)
	{
		NextThink = Now + 0.2f;
		Think(Hero, PS);
	}
	if (bHasGoal) { MoveTowards(Hero, Goal, DeltaSeconds); }
	const FVector ToAim = Hero->GetAim() - Hero->GetActorLocation();
	if (ToAim.SizeSquared2D() > 25.f) { SetControlRotation(FRotator(0.f, ToAim.Rotation().Yaw, 0.f)); }
}

void ABotSBotController::MoveTowards(ABotSHeroCharacter* Hero, const FVector& InGoal, float DeltaSeconds)
{
	const FVector Loc = Hero->GetActorLocation();
	const FVector2D LocM = BotSToM(Loc), GoalM = BotSToM(InGoal);
	const float Now = GetWorld()->GetTimeSeconds();
	FVector2D Target = GoalM;
	const bool bDirect = FVector2D::Distance(LocM, GoalM) < 16.f && HasLineOfSight(Loc, InGoal);
	if (!bDirect)
	{
		if (Path.Num() == 0 || Now >= NextRepath || FVector2D::Distance(PathGoal, GoalM) > 3.f)
		{
			FBotSMapLayout::Get().FindPath(LocM, GoalM, Path);
			PathGoal = GoalM;
			NextRepath = Now + 1.5f;
		}
		while (Path.Num() > 0 && FVector2D::Distance(LocM, Path[0]) < 1.f) { Path.RemoveAt(0); }
		if (Path.Num() > 0) { Target = Path[0]; }
	}
	else
	{
		Path.Reset();
	}
	const FVector Dir = FVector(Target.X - LocM.X, Target.Y - LocM.Y, 0.f).GetSafeNormal();
	Hero->AddMovementInput(Dir);
}

static bool BotSWantsPickup(const ABotSPlayerState* PS, const ABotSPickup* Pickup)
{
	const FBotSItemDef* Def = BotSData().FindItem(Pickup->ItemId);
	const UBotSInventoryComponent* Inv = PS->GetInventory();
	if (!Def) { return false; }
	if (Def->Kind != EBotSItemKind::Active)
	{
		// Nur leere Slots fuellen oder die Seltenheit verbessern, sonst tauscht der Bot endlos.
		const FBotSItemSlot& Slot = Inv->Relics[int32(Def->Kind)];
		const FBotSItemDef* Current = Slot.Item.IsNone() ? nullptr : BotSData().FindItem(Slot.Item);
		return !Current || Def->Rarity > Current->Rarity;
	}
	for (const FBotSItemSlot& Slot : Inv->Actives)
	{
		if (Slot.Item.IsNone() || (Slot.Item == Pickup->ItemId && Slot.Charges < Def->MaxCharges)) { return true; }
	}
	return false;
}

void ABotSBotController::Think(ABotSHeroCharacter* Hero, ABotSPlayerState* PS)
{
	UWorld* World = GetWorld();
	const ABotSGameState* G = World->GetGameState<ABotSGameState>();
	const UBotSAttributeSet* Attr = PS->GetAttributes();
	if (!G || !Attr || G->Phase != EBotSMatchPhase::Match)
	{
		bHasGoal = false;
		Hero->SetAttacking(false);
		return;
	}
	const float Now = World->GetTimeSeconds();
	const FVector Loc = Hero->GetActorLocation();
	const FVector2D LocM = BotSToM(Loc);
	const float HealthPct = Attr->GetHealth() / FMath::Max(1.f, Attr->GetMaxHealth());
	const int32 Team = Hero->GetTeam();

	// Talente: Bots waehlen zufaellig.
	const int32 Tier = PS->GetOpenTalentTier();
	if (Tier >= 0) { PS->ChooseTalent(Tier, FMath::RandRange(0, 1)); }
	if (HealthPct < 0.5f) { Hero->TryUseSlot(EBotSSlot::Item1, Loc); Hero->TryUseSlot(EBotSSlot::Item2, Loc); }

	// Sturm: rechtzeitig ins Auge.
	const FBotSStormState& Storm = G->Storm;
	const float ServerNow = G->GetServerWorldTimeSeconds();
	const float DistToEye = FVector2D::Distance(LocM, Storm.ToCenter);
	const bool bUrgent = PS->bInStorm || ServerNow >= Storm.ShrinkStart - 45.f;
	const bool bMustMove = Storm.Phase > 0 && bUrgent && DistToEye > FMath::Max(2.f, Storm.ToRadius * 0.8f);
	if (Now >= NextWander)
	{
		NextWander = Now + 8.f;
		const FVector2D R = FMath::RandPointInCircle(FMath::Max(1.f, Storm.ToRadius * 0.6f));
		WanderOffset = FVector(R.X, R.Y, 0.f);
	}
	const FVector EyeGoal = BotSM(Storm.ToCenter + FVector2D(WanderOffset.X, WanderOffset.Y) * (bMustMove ? 0.5f : 1.f), Loc.Z);

	// Wahrnehmung.
	ABotSCombatCharacter* Enemy = nullptr;
	float EnemyDist = 18.f;
	ABotSHeroCharacter* DownedMate = nullptr;
	for (TActorIterator<ABotSCombatCharacter> It(World); It; ++It)
	{
		ABotSCombatCharacter* C = *It;
		if (C == Hero || !C->IsTargetable()) { continue; }
		const float D = FVector::Dist2D(C->GetActorLocation(), Loc) / 100.f;
		ABotSHeroCharacter* OtherHero = Cast<ABotSHeroCharacter>(C);
		if (C->GetTeam() == Team)
		{
			if (OtherHero && OtherHero->IsDowned() && D < 30.f) { DownedMate = OtherHero; }
			continue;
		}
		// Kreaturen nur aus der Naehe und bei guter Gesundheit, niedergeschlagene Gegner mit Nachrang.
		float Score = D;
		if (!OtherHero) { if (D > 9.f || HealthPct < 0.5f || bMustMove) { continue; } Score += 4.f; }
		else if (C->IsDowned()) { Score += 6.f; }
		if (Score < EnemyDist && HasLineOfSight(Loc, C->GetActorLocation())) { EnemyDist = Score; Enemy = C; }
	}

	bool bHold = false;
	bool bPress = false;
	bWantStormRun = false;
	if (Enemy && !(bMustMove && PS->bInStorm))
	{
		const FVector EnemyLoc = Enemy->GetActorLocation();
		const float Dist = FVector::Dist2D(EnemyLoc, Loc) / 100.f;
		const float Range = Attr->GetAttackRange();
		Hero->SetAim(EnemyLoc);
		Hero->SetAttacking(Dist <= Range + 0.6f);
		if (Range < 3.f)
		{
			bHasGoal = Dist > 1.2f;
			Goal = EnemyLoc;
		}
		else if (Dist > Range * 0.9f) { bHasGoal = true; Goal = EnemyLoc; }
		else if (Dist < Range * 0.4f) { bHasGoal = true; Goal = Loc + (Loc - EnemyLoc).GetSafeNormal2D() * 300.f; }
		else { bHasGoal = false; }
		if (FMath::FRand() < 0.7f) { TryAbilities(Hero, PS, Enemy, Dist); }
	}
	else
	{
		Hero->SetAttacking(false);
		if (bMustMove)
		{
			bHasGoal = true;
			Goal = EyeGoal;
			bWantStormRun = true;
		}
		else if (DownedMate)
		{
			const float D = FVector::Dist2D(DownedMate->GetActorLocation(), Loc);
			bHasGoal = D > 150.f;
			Goal = DownedMate->GetActorLocation();
			bHold = !bHasGoal;
		}
		else
		{
			// Beute: Gegenstaende in der Naehe, dann Truhen.
			AActor* Loot = nullptr;
			float Best = 12.f * 100.f;
			for (TActorIterator<ABotSPickup> It(World); It; ++It)
			{
				const float D = FVector::Dist2D(It->GetActorLocation(), Loc);
				if (D < Best && BotSWantsPickup(PS, *It)) { Best = D; Loot = *It; }
			}
			if (!Loot)
			{
				Best = 70.f * 100.f;
				for (TActorIterator<ABotSLootChest> It(World); It; ++It)
				{
					const float D = FVector::Dist2D(It->GetActorLocation(), Loc);
					const bool bSafe = Storm.Phase <= 0 || FVector2D::Distance(BotSToM(It->GetActorLocation()), Storm.ToCenter) < Storm.ToRadius;
					if (!It->bOpened && bSafe && D < Best) { Best = D; Loot = *It; }
				}
			}
			if (Loot)
			{
				bHasGoal = Best > 140.f;
				Goal = Loot->GetActorLocation();
				bPress = !bHasGoal;
			}
			else
			{
				bHasGoal = FVector::Dist2D(EyeGoal, Loc) > 300.f;
				Goal = EyeGoal;
				bWantStormRun = FVector::Dist2D(EyeGoal, Loc) > 4000.f;
			}
		}
		if (bHasGoal) { Hero->SetAim(Loc + (Goal - Loc).GetSafeNormal2D() * 500.f); }
	}

	// Interagieren: Wiederbeleben wird gehalten, Aufheben und Oeffnen sind ein Tastendruck.
	if (bHold)
	{
		if (Hero->ReviveEndTime <= 0.f) { Hero->SetInteractHeld(false); Hero->SetInteractHeld(true); }
	}
	else if (bPress && Now >= NextInteract)
	{
		NextInteract = Now + 0.5f;
		Hero->SetInteractHeld(false);
		Hero->SetInteractHeld(true);
	}
	else if (!bPress)
	{
		Hero->SetInteractHeld(false);
	}
	if (bWantStormRun && !Hero->bStormRunAllowed) { Hero->StartStormRun(); }
}

void ABotSBotController::TryAbilities(ABotSHeroCharacter* Hero, ABotSPlayerState* PS, ABotSCombatCharacter* Enemy, float EnemyDistM)
{
	const FVector Loc = Hero->GetActorLocation();
	const FVector EnemyLoc = Enemy->GetActorLocation();
	const bool bMelee = PS->GetAttributes()->GetAttackRange() < 3.f;
	for (EBotSSlot Slot : { EBotSSlot::Ultimate, EBotSSlot::Ability1, EBotSSlot::Ability2, EBotSSlot::Ability3 })
	{
		FBotSAbilityDef Def;
		float Duration;
		if (!PS->GetAbilityDef(Slot, Def) || PS->GetCooldownRemaining(Slot, Duration) > 0.f) { continue; }
		const float Range = Def.P("Range"), Radius = Def.P("Radius", 3.f);
		FVector Aim = EnemyLoc;
		bool bUse = false;

		if (Def.P("Heal") > 0.f)
		{
			// Heilung: schwaechsten Verbuendeten in Reichweite suchen.
			float Lowest = 0.7f;
			for (TActorIterator<ABotSHeroCharacter> It(GetWorld()); It; ++It)
			{
				if (It->GetTeam() != Hero->GetTeam() || !It->IsTargetable() || It->IsDowned()) { continue; }
				const float Pct = It->GetHealth() / FMath::Max(1.f, It->GetMaxHealth());
				if (Pct < Lowest && FVector::Dist2D(It->GetActorLocation(), Loc) <= BotSM(Range)) { Lowest = Pct; Aim = It->GetActorLocation(); bUse = true; }
			}
		}
		else if (Def.Type == EBotSAbilityType::Dash)
		{
			if (bMelee) { bUse = EnemyDistM > 3.f && EnemyDistM < Def.P("Distance", 5.f) + 1.5f; }
			else if (EnemyDistM < 3.5f) { bUse = true; Aim = Loc + (Loc - EnemyLoc).GetSafeNormal2D() * 1000.f; }
		}
		else if (Def.Type == EBotSAbilityType::Projectile)
		{
			bUse = EnemyDistM <= Range;
			// Vorhalten: Ziel bewegt sich waehrend der Flugzeit weiter.
			Aim = EnemyLoc + Enemy->GetVelocity() * (EnemyDistM / FMath::Max(1.f, Def.P("Speed", 20.f)));
		}
		else if (Def.Type == EBotSAbilityType::Area)
		{
			bUse = Range > 0.f ? EnemyDistM <= Range + Radius * 0.5f : EnemyDistM <= FMath::Max(Radius, Def.P("Damage") > 0.f ? Radius : 12.f);
		}
		if (bUse && Hero->TryUseSlot(Slot, Aim))
		{
			Hero->SetAim(EnemyLoc);
			return;
		}
	}
}
