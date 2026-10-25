#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TharrnPersistenceComponent.generated.h"

/**
 * Marks an actor as part of the shared world save and gives it a stable identity.
 * Authored actors (the district's nodes and containers) derive their id from the layout and are matched on load;
 * runtime actors (loot caches, later built structures) carry a new GUID and are recreated by the save.
 */
UCLASS(ClassGroup = (Tharrn), meta = (BlueprintSpawnableComponent))
class THARRN_API UTharrnPersistenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTharrnPersistenceComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame) FGuid PersistentId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame) bool bRuntimeSpawned = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame) FString DebugKey;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
