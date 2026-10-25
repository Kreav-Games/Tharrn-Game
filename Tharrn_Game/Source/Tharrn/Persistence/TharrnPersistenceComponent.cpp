#include "Persistence/TharrnPersistenceComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Persistence/TharrnSaveSubsystem.h"

UTharrnPersistenceComponent::UTharrnPersistenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTharrnPersistenceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// An authored actor destroyed in play (a cleared barrier, a looted cache) must stay gone after a reload.
	if (EndPlayReason == EEndPlayReason::Destroyed && !bRuntimeSpawned && PersistentId.IsValid())
	{
		if (const AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
		{
			if (UTharrnSaveSubsystem* Saves = GetWorld() ? GetWorld()->GetSubsystem<UTharrnSaveSubsystem>() : nullptr)
			{
				Saves->RegisterDestroyed(PersistentId);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}
