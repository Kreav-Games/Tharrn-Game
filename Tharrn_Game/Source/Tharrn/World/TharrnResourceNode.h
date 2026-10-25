#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Persistence/TharrnSaveAware.h"
#include "World/TharrnInteractable.h"
#include "TharrnResourceNode.generated.h"

class UStaticMeshComponent;
class UTharrnPersistenceComponent;

/**
 * A gatherable deposit, plant or salvage pile. Each gather spends one charge and yields a fixed amount; an empty node
 * regrows after RespawnSeconds. A gather is refused before any charge is spent if the yield does not fit.
 */
UCLASS()
class THARRN_API ATharrnResourceNode : public AActor, public ITharrnInteractable, public ITharrnSaveAware
{
	GENERATED_BODY()

public:
	ATharrnResourceNode();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UTharrnPersistenceComponent> Persistence;

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_State, SaveGame) FName ResourceDefId;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_State, SaveGame) int32 ChargesRemaining = -1;
	/** Seconds until regrowth, written at save time from the live timer. */
	UPROPERTY(SaveGame) float RespawnRemaining = 0.f;

	const struct FTharrnResourceDef* GetDef() const;
	bool IsDepleted() const { return ChargesRemaining == 0; }
	void Regrow();

	virtual FString GetInteractPrompt(const ATharrnCharacterBase* User) const override;
	virtual FString GetBlockedReason(const ATharrnCharacterBase* User) const override;
	virtual float GetInteractDuration(const ATharrnCharacterBase* User) const override;
	virtual bool Interact(ATharrnCharacterBase* User, FString& OutMessage) override;

	virtual void TharrnPreSave() override;
	virtual void TharrnPostLoad() override;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_State();
	void RefreshVisual();
	void ScheduleRegrow(float Seconds);

	FTimerHandle RegrowTimer;
	FName AppliedDef;
};
