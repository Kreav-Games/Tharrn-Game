#include "Characters/TharrnCreature.h"

#include "AIController.h"
#include "Combat/TharrnCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Net/UnrealNetwork.h"
#include "Survival/TharrnVitalsComponent.h"
#include "World/TharrnContainer.h"
#include "World/TharrnCreatureSpawner.h"

ATharrnCreature::ATharrnCreature()
{
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	Faction = ETharrnFaction::Hostile;
	bShowTelegraph = true;
	Vitals->bHasNeeds = false;
	Inventory->MaxSlots = 4;
	GetCharacterMovement()->bUseControllerDesiredRotation = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	WeaponMesh->SetVisibility(false);
}

void ATharrnCreature::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnCreature, CreatureDefId);
	DOREPLIFETIME(ATharrnCreature, State);
}

void ATharrnCreature::BeginPlay()
{
	ApplyDef();
	Super::BeginPlay();
	if (HasAuthority())
	{
		if (HomeLocation.IsZero())
		{
			HomeLocation = GetActorLocation();
		}
		NextWanderTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(2.f, 5.f);
	}
}

void ATharrnCreature::OnRep_Def()
{
	ApplyDef();
	BaseColor = Def.Color;
}

void ATharrnCreature::ApplyDef()
{
	const FTharrnCreatureDef* Found = FTharrnData::Get().FindCreature(CreatureDefId);
	if (!Found || bApplied)
	{
		return;
	}
	bApplied = true;
	Def = *Found;
	Faction = Def.bHostile ? ETharrnFaction::Hostile : ETharrnFaction::Wildlife;
	BaseColor = Def.Color;
	WalkSpeed = Def.WalkSpeed;
	RunSpeed = Def.RunSpeed;
	Combat->MaxPoise = Def.MaxPoise;
	if (HasAuthority())
	{
		Vitals->InitPools(Def.MaxHealth, 100.f, false);
	}
	// Low, wide greybox body with the head forward so facing reads at a glance.
	const FVector Scale = Def.BodyScale;
	GetCapsuleComponent()->SetCapsuleSize(45.f * Scale.Y, 60.f * Scale.Z);
	BodyMesh->SetStaticMesh(TharrnStatics::BasicShape(TEXT("Cube")));
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -10.f * Scale.Z));
	BodyMesh->SetRelativeScale3D(FVector(1.1f * Scale.X, 0.8f * Scale.Y, 0.7f * Scale.Z));
	HeadMesh->SetRelativeLocation(FVector(65.f * Scale.X, 0.f, 10.f * Scale.Z));
	HeadMesh->SetRelativeScale3D(FVector(0.55f * Scale.Y));
}

void ATharrnCreature::SetBrainEnabled(bool bEnabled)
{
	bBrainEnabled = bEnabled;
	if (!bEnabled)
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->StopMovement();
		}
		bDirectChase = false;
		bWantsSprint = false;
	}
}

void ATharrnCreature::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || IsDead() || !bBrainEnabled)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextThinkTime)
	{
		NextThinkTime = Now + 0.25f;
		Think();
	}
	// Turn into the target while winding up, so the telegraph points at who it threatens.
	if (ATharrnCharacterBase* Victim = Target.Get(); Victim && Combat->Phase == ETharrnAttackPhase::Windup)
	{
		const FRotator Want = (Victim->GetActorLocation() - GetActorLocation()).GetSafeNormal2D().Rotation();
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Want.Yaw, 0.f), DeltaSeconds, 6.f));
	}
	if (bDirectChase && !Combat->IsAttacking())
	{
		const FVector ToGoal = (DirectGoal - GetActorLocation()).GetSafeNormal2D();
		AddMovementInput(ToGoal, 1.f);
	}
}

ATharrnCharacterBase* ATharrnCreature::FindTarget() const
{
	if (!Def.bHostile)
	{
		return nullptr;
	}
	ATharrnCharacterBase* Best = nullptr;
	float BestDistSq = FMath::Square(Def.AggroRadius);
	for (TActorIterator<ATharrnCharacterBase> It(GetWorld()); It; ++It)
	{
		ATharrnCharacterBase* Candidate = *It;
		if (Candidate && !Candidate->IsDead() && IsHostileTo(Candidate)
			&& FVector::DistSquared(Candidate->GetActorLocation(), HomeLocation) < FMath::Square(Def.LeashRadius))
		{
			const float DistSq = FVector::DistSquared(Candidate->GetActorLocation(), GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Candidate;
			}
		}
	}
	return Best;
}

void ATharrnCreature::MoveTowards(const FVector& Location, AActor* Goal, float Acceptance)
{
	AAIController* AI = Cast<AAIController>(GetController());
	EPathFollowingRequestResult::Type Result = EPathFollowingRequestResult::Failed;
	if (AI)
	{
		Result = Goal ? AI->MoveToActor(Goal, Acceptance, true, true, false) : AI->MoveToLocation(Location, Acceptance);
	}
	// Without a navigable path, steer straight at the goal so the creature never freezes.
	bDirectChase = Result == EPathFollowingRequestResult::Failed;
	bDirectChaseUsed |= bDirectChase;
	bPathSucceeded |= Result != EPathFollowingRequestResult::Failed;
	DirectGoal = Goal ? Goal->GetActorLocation() : Location;
}

void ATharrnCreature::Think()
{
	const float Now = GetWorld()->GetTimeSeconds();
	AAIController* AI = Cast<AAIController>(GetController());
	ATharrnCharacterBase* Victim = Target.Get();

	const bool bBeyondLeash = FVector::DistSquared(GetActorLocation(), HomeLocation) > FMath::Square(Def.LeashRadius);
	if (!Victim || Victim->IsDead() || bBeyondLeash
		|| FVector::DistSquared(Victim->GetActorLocation(), HomeLocation) > FMath::Square(Def.LeashRadius))
	{
		Target = nullptr;
		Victim = bBeyondLeash ? nullptr : FindTarget();
		Target = Victim;
	}

	if (Victim)
	{
		State = ETharrnCreatureState::Chase;
		bWantsSprint = true;
		const float Reach = Def.Attack.Range * 0.85f + Victim->GetBodyRadius();
		const float Distance = FVector::Dist2D(Victim->GetActorLocation(), GetActorLocation());
		if (Combat->IsAttacking() || Combat->bStaggered)
		{
			return;
		}
		if (Distance <= Reach && Now >= NextAttackTime)
		{
			if (AI)
			{
				AI->StopMovement();
			}
			bDirectChase = false;
			SetActorRotation(FRotator(0.f, (Victim->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
			if (Combat->TryStartAttack(Def.Attack))
			{
				++TelegraphCount;
				NextAttackTime = Now + Def.Attack.Windup + Def.Attack.Active + Def.Attack.Recovery + Def.AttackCooldown;
			}
			return;
		}
		if (Distance > Reach * 0.7f)
		{
			MoveTowards(Victim->GetActorLocation(), Victim, Def.Attack.Range * 0.5f);
		}
		return;
	}

	bWantsSprint = false;
	if (FVector::Dist2D(GetActorLocation(), HomeLocation) > Def.WanderRadius * 1.5f)
	{
		State = ETharrnCreatureState::Return;
		MoveTowards(HomeLocation, nullptr, 80.f);
		return;
	}
	if (Now >= NextWanderTime)
	{
		NextWanderTime = Now + FMath::FRandRange(4.f, 7.f);
		FNavLocation Point;
		const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (Nav && Nav->GetRandomReachablePointInRadius(HomeLocation, Def.WanderRadius, Point))
		{
			State = ETharrnCreatureState::Wander;
			MoveTowards(Point.Location, nullptr, 50.f);
		}
		else
		{
			State = ETharrnCreatureState::Idle;
			bDirectChase = false;
		}
	}
	else if (State == ETharrnCreatureState::Return && bDirectChase && FVector::Dist2D(GetActorLocation(), HomeLocation) < 150.f)
	{
		bDirectChase = false;
		State = ETharrnCreatureState::Idle;
	}
}

void ATharrnCreature::HandleDeath(AActor* Killer)
{
	Super::HandleDeath(Killer);
	State = ETharrnCreatureState::Dead;
	bDirectChase = false;
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Remains hold the drops; they persist with the world save until emptied.
	if (Def.Drops.Num() > 0)
	{
		const FVector Ground = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 30.f);
		if (ATharrnContainer* Remains = ATharrnContainer::SpawnCache(GetWorld(), TEXT("creature_remains"), Ground, FString::Printf(TEXT("%s remains"), *GetDisplayName())))
		{
			for (const FTharrnItemAmount& Drop : Def.Drops)
			{
				Remains->Inventory->AddItem(Drop.ItemId, Drop.Count);
			}
		}
	}
	if (ATharrnCreatureSpawner* Home = Spawner.Get())
	{
		Home->OnCreatureDied(this);
	}
	SetLifeSpan(3.f);
}
