#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Persistence/TharrnSaveAware.h"
#include "World/TharrnInteractable.h"
#include "TharrnContainer.generated.h"

class UStaticMeshComponent;
class UTharrnInventoryComponent;
class UTharrnPersistenceComponent;

/**
 * Storage, supply crates and caches (creature remains, the pack left where a player was injured).
 * Initial contents are granted exactly once per campaign: the grant flag is saved with the contents.
 */
UCLASS()
class THARRN_API ATharrnContainer : public AActor, public ITharrnInteractable, public ITharrnSaveAware
{
	GENERATED_BODY()

public:
	ATharrnContainer();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UTharrnInventoryComponent> Inventory;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTharrnPersistenceComponent> Persistence;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Def, SaveGame) FName ContainerDefId;
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) FString DisplayName;
	UPROPERTY(SaveGame) bool bInitialContentsGranted = false;
	UPROPERTY(Replicated, SaveGame) bool bDestroyWhenEmpty = false;

	/** Spawns a runtime cache (saved and recreated by the world save). */
	static ATharrnContainer* SpawnCache(UWorld* World, FName DefId, const FVector& Location, const FString& NameOverride = FString());

	virtual FString GetInteractPrompt(const ATharrnCharacterBase* User) const override;
	virtual bool Interact(ATharrnCharacterBase* User, FString& OutMessage) override;

	virtual void TharrnPostLoad() override;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Def();
	UFUNCTION() void HandleInventoryChanged();
	void ApplyDef();
	FName AppliedDef;
};
