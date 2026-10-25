#include "World/TharrnCreatureSpawner.h"

#include "Characters/TharrnCreature.h"
#include "Components/SceneComponent.h"
#include "Core/TharrnData.h"
#include "Engine/World.h"
#include "TimerManager.h"

ATharrnCreatureSpawner::ATharrnCreatureSpawner()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ATharrnCreatureSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		SpawnUpToMax();
	}
}

void ATharrnCreatureSpawner::SpawnUpToMax()
{
	Alive.RemoveAll([](const TWeakObjectPtr<ATharrnCreature>& C) { return !C.IsValid() || C->IsDead(); });
	while (Alive.Num() < MaxAlive)
	{
		if (!SpawnOne())
		{
			break;
		}
	}
}

ATharrnCreature* ATharrnCreatureSpawner::SpawnOne()
{
	UWorld* World = GetWorld();
	if (!World || !FTharrnData::Get().FindCreature(CreatureDefId))
	{
		return nullptr;
	}
	const float Angle = (SpawnSerial++ * 137.5f);
	const FVector Offset = FRotator(0.f, Angle, 0.f).Vector() * SpawnRadius * (Alive.Num() > 0 ? 1.f : 0.f);
	const FTransform SpawnTransform(FRotator(0.f, GetActorRotation().Yaw, 0.f), GetActorLocation() + Offset + FVector(0.f, 0.f, 100.f));
	ATharrnCreature* Creature = World->SpawnActorDeferred<ATharrnCreature>(ATharrnCreature::StaticClass(), SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Creature)
	{
		return nullptr;
	}
	Creature->CreatureDefId = CreatureDefId;
	Creature->Spawner = this;
	Creature->HomeLocation = GetActorLocation();
	Creature->FinishSpawning(SpawnTransform);
	Alive.Add(Creature);
	return Creature;
}

void ATharrnCreatureSpawner::OnCreatureDied(ATharrnCreature* Creature)
{
	const FTharrnCreatureDef* Def = FTharrnData::Get().FindCreature(CreatureDefId);
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ATharrnCreatureSpawner::SpawnUpToMax, Def ? FMath::Max(1.f, Def->RespawnSeconds) : 120.f, false);
}
