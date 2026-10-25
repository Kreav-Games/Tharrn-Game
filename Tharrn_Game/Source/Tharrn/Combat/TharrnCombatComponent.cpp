#include "Combat/TharrnCombatComponent.h"

#include "Characters/TharrnCharacterBase.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Survival/TharrnVitalsComponent.h"

UTharrnCombatComponent::UTharrnCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UTharrnCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTharrnCombatComponent, Phase);
	DOREPLIFETIME(UTharrnCombatComponent, PhaseStartTime);
	DOREPLIFETIME(UTharrnCombatComponent, PhaseDuration);
	DOREPLIFETIME(UTharrnCombatComponent, bBlocking);
	DOREPLIFETIME(UTharrnCombatComponent, bStaggered);
	DOREPLIFETIME(UTharrnCombatComponent, bDodging);
}

void UTharrnCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Poise = MaxPoise;
}

float UTharrnCombatComponent::Now() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

bool UTharrnCombatComponent::CanAct() const
{
	const UTharrnVitalsComponent* Vitals = GetOwner() ? GetOwner()->FindComponentByClass<UTharrnVitalsComponent>() : nullptr;
	return !bStaggered && !bDodging && Phase == ETharrnAttackPhase::None && !(Vitals && Vitals->bDead);
}

bool UTharrnCombatComponent::IsInvulnerable() const
{
	return Now() < InvulnerableUntil;
}

float UTharrnCombatComponent::GetPhaseAlpha() const
{
	if (Phase == ETharrnAttackPhase::None || PhaseDuration <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp((TharrnStatics::ServerTime(GetWorld()) - PhaseStartTime) / PhaseDuration, 0.f, 1.f);
}

void UTharrnCombatComponent::EnterPhase(ETharrnAttackPhase NewPhase, float Duration)
{
	Phase = NewPhase;
	PhaseDuration = FMath::Max(0.01f, Duration);
	PhaseStartTime = TharrnStatics::ServerTime(GetWorld());
	PhaseEndTime = Now() + PhaseDuration;
	if (NewPhase == ETharrnAttackPhase::None)
	{
		PhaseDuration = 0.f;
	}
}

bool UTharrnCombatComponent::TryStartAttack(const FTharrnMeleeStats& Stats)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !CanAct() || bBlocking)
	{
		return false;
	}
	UTharrnVitalsComponent* Vitals = Owner->FindComponentByClass<UTharrnVitalsComponent>();
	if (Vitals && !Vitals->TrySpendStamina(Stats.StaminaCost))
	{
		return false;
	}
	CurrentAttack = Stats;
	HitThisSwing.Reset();
	++SwingCount;
	EnterPhase(ETharrnAttackPhase::Windup, Stats.Windup);
	return true;
}

void UTharrnCombatComponent::CancelAttack()
{
	EnterPhase(ETharrnAttackPhase::None, 0.f);
	HitThisSwing.Reset();
}

void UTharrnCombatComponent::SetBlocking(bool bNewBlocking)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	if (bNewBlocking && (bStaggered || bDodging || Phase != ETharrnAttackPhase::None))
	{
		return;
	}
	if (bNewBlocking && !bBlocking)
	{
		BlockStartTime = Now();
	}
	bBlocking = bNewBlocking;
	if (UTharrnVitalsComponent* Vitals = Owner->FindComponentByClass<UTharrnVitalsComponent>())
	{
		Vitals->SetRegenScale(bBlocking ? FTharrnData::Get().GetSurvival().BlockingStaminaRegenScale : 1.f);
	}
}

bool UTharrnCombatComponent::TryDodge(const FVector& Direction)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->HasAuthority() || !CanAct() || bBlocking)
	{
		return false;
	}
	UTharrnVitalsComponent* Vitals = Character->FindComponentByClass<UTharrnVitalsComponent>();
	if (Vitals && !Vitals->TrySpendStamina(DodgeStaminaCost))
	{
		return false;
	}
	FVector Dir = Direction.GetSafeNormal2D();
	if (Dir.IsNearlyZero())
	{
		Dir = -Character->GetActorForwardVector();
	}
	bDodging = true;
	InvulnerableUntil = Now() + DodgeInvulnerability;
	DodgeEndTime = Now() + DodgeInvulnerability + 0.1f;
	Character->LaunchCharacter(Dir * DodgeSpeed + FVector(0.f, 0.f, 120.f), true, false);
	return true;
}

void UTharrnCombatComponent::ApplyStagger(float Seconds, const FVector& PushDirection)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	CancelAttack();
	bBlocking = false;
	bStaggered = true;
	StaggerEndTime = FMath::Max(StaggerEndTime, Now() + Seconds);
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		Character->LaunchCharacter(PushDirection.GetSafeNormal2D() * 350.f + FVector(0.f, 0.f, 80.f), true, false);
	}
}

void UTharrnCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	const float Time = Now();
	Poise = FMath::Min(MaxPoise, Poise + PoiseRegenPerSecond * DeltaTime);
	if (bStaggered && Time >= StaggerEndTime)
	{
		bStaggered = false;
	}
	if (bDodging && Time >= DodgeEndTime)
	{
		bDodging = false;
	}

	switch (Phase)
	{
	case ETharrnAttackPhase::Windup:
		if (Time >= PhaseEndTime)
		{
			EnterPhase(ETharrnAttackPhase::Active, CurrentAttack.Active);
			if (CurrentAttack.Lunge > 0.f)
			{
				if (ACharacter* Character = Cast<ACharacter>(Owner))
				{
					Character->LaunchCharacter(Owner->GetActorForwardVector() * CurrentAttack.Lunge, true, false);
				}
			}
			DetectHits();
		}
		break;
	case ETharrnAttackPhase::Active:
		DetectHits();
		if (Time >= PhaseEndTime)
		{
			EnterPhase(ETharrnAttackPhase::Recovery, CurrentAttack.Recovery);
		}
		break;
	case ETharrnAttackPhase::Recovery:
		if (Time >= PhaseEndTime)
		{
			EnterPhase(ETharrnAttackPhase::None, 0.f);
		}
		break;
	default:
		break;
	}
}

void UTharrnCombatComponent::DetectHits()
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}
	const ATharrnCharacterBase* Self = Cast<ATharrnCharacterBase>(Owner);
	const FVector Origin = Owner->GetActorLocation();
	const FVector Forward = Owner->GetActorForwardVector();
	const float Reach = CurrentAttack.Range;
	const float CosHalfArc = FMath::Cos(FMath::DegreesToRadians(CurrentAttack.ArcDegrees * 0.5f));

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TharrnMelee), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Origin + Forward * Reach * 0.5f, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Reach * 0.75f), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Victim = Overlap.GetActor();
		if (!Victim || Victim == Owner || HitThisSwing.Contains(Victim))
		{
			continue;
		}
		const ATharrnCharacterBase* Target = Cast<ATharrnCharacterBase>(Victim);
		if (!Target || (Self && !Self->IsHostileTo(Target)))
		{
			continue;
		}
		FVector ToVictim = Victim->GetActorLocation() - Origin;
		ToVictim.Z = 0.f;
		const float Distance = ToVictim.Size();
		if (Distance > Reach + Target->GetBodyRadius())
		{
			continue;
		}
		if (Distance > 40.f && FVector::DotProduct(ToVictim / Distance, Forward) < CosHalfArc)
		{
			continue;
		}
		HitThisSwing.Add(Victim);
		float Damage = 0.f;
		const ETharrnHitOutcome Outcome = ResolveHit(Owner, Victim, CurrentAttack, Damage);
		OnLandedHit.Broadcast(Victim, Outcome);
	}
}

ETharrnHitOutcome UTharrnCombatComponent::ResolveHit(AActor* Attacker, AActor* Victim, const FTharrnMeleeStats& Stats, float& OutDamage)
{
	OutDamage = 0.f;
	UTharrnVitalsComponent* VictimVitals = Victim ? Victim->FindComponentByClass<UTharrnVitalsComponent>() : nullptr;
	if (!Attacker || !VictimVitals || VictimVitals->bDead || !Victim->HasAuthority())
	{
		return ETharrnHitOutcome::None;
	}
	UTharrnCombatComponent* VictimCombat = Victim->FindComponentByClass<UTharrnCombatComponent>();
	const FVector ToAttacker = (Attacker->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal2D();
	const bool bFacing = FVector::DotProduct(Victim->GetActorForwardVector(), ToAttacker) > 0.25f;

	float Damage = Stats.Damage;
	ETharrnHitOutcome Outcome = ETharrnHitOutcome::Hit;
	if (VictimCombat)
	{
		if (VictimCombat->IsInvulnerable())
		{
			Outcome = ETharrnHitOutcome::Dodged;
			Damage = 0.f;
		}
		else if (VictimCombat->bBlocking && bFacing)
		{
			if (VictimCombat->Now() - VictimCombat->BlockStartTime <= VictimCombat->ParryWindow)
			{
				Outcome = ETharrnHitOutcome::Parried;
				Damage = 0.f;
				if (UTharrnCombatComponent* AttackerCombat = Attacker->FindComponentByClass<UTharrnCombatComponent>())
				{
					AttackerCombat->ApplyStagger(AttackerCombat->StaggerSeconds * 1.4f, ToAttacker);
				}
			}
			else
			{
				const float Cost = Stats.Damage * VictimCombat->BlockStaminaPerDamage;
				if (VictimVitals->Stamina >= Cost)
				{
					VictimVitals->DrainStamina(Cost);
					Damage *= 1.f - VictimCombat->BlockDamageReduction;
					Outcome = ETharrnHitOutcome::Blocked;
				}
				else
				{
					VictimVitals->DrainStamina(VictimVitals->Stamina);
					VictimCombat->ApplyStagger(VictimCombat->StaggerSeconds, -ToAttacker);
					Damage *= 0.5f;
					Outcome = ETharrnHitOutcome::GuardBroken;
				}
			}
		}
		else
		{
			VictimCombat->Poise -= Stats.Poise;
			if (VictimCombat->Poise <= 0.f)
			{
				VictimCombat->ApplyStagger(VictimCombat->StaggerSeconds, -ToAttacker);
				VictimCombat->Poise = VictimCombat->MaxPoise;
				Outcome = ETharrnHitOutcome::Staggered;
			}
		}
	}
	OutDamage = VictimVitals->ApplyDamage(Damage, Attacker);
	if (ATharrnCharacterBase* VictimCharacter = Cast<ATharrnCharacterBase>(Victim))
	{
		VictimCharacter->MulticastCombatEvent(Outcome, OutDamage);
	}
	return Outcome;
}
