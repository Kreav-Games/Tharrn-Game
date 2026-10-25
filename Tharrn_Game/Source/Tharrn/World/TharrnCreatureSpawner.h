#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TharrnCreatureSpawner.generated.h"

class ATharrnCreature;

/** Keeps up to MaxAlive creatures of one kind near its location; replaces the dead after the creature's respawn time. Server only. */
UCLASS()
class THARRN_API ATharrnCreatureSpawner : public AActor
{
	GENERATED_BODY()

public:
	ATharrnCreatureSpawner();

	UPROPERTY(EditAnywhere) FName CreatureDefId;
	UPROPERTY(EditAnywhere) int32 MaxAlive = 1;
	UPROPERTY(EditAnywhere) float SpawnRadius = 250.f;
	UPROPERTY(EditAnywhere) FString StableKey;

	const TArray<TWeakObjectPtr<ATharrnCreature>>& GetAlive() const { return Alive; }
	void OnCreatureDied(ATharrnCreature* Creature);
	ATharrnCreature* SpawnOne();
	void SpawnUpToMax();

	virtual void BeginPlay() override;

protected:
	TArray<TWeakObjectPtr<ATharrnCreature>> Alive;
	FTimerHandle RespawnTimer;
	int32 SpawnSerial = 0;
};
