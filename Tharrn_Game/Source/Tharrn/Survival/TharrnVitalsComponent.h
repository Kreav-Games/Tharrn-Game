#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TharrnTypes.h"
#include "TharrnVitalsComponent.generated.h"

USTRUCT(BlueprintType)
struct FTharrnActiveEffect
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame) FName EffectId;
	UPROPERTY(BlueprintReadOnly, SaveGame) float Remaining = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTharrnDied, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTharrnDamaged, float, Amount, AActor*, Instigator);

/**
 * Health, stamina, food and water for every living thing. Creatures switch needs off.
 * The server simulates; clients only display the replicated values.
 */
UCLASS(ClassGroup = (Tharrn), meta = (BlueprintSpawnableComponent))
class THARRN_API UTharrnVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTharrnVitalsComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated) float MaxHealth = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated) float MaxStamina = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated) float MaxFood = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated) float MaxWater = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bHasNeeds = true;

	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) float Health = 100.f;
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) float Stamina = 100.f;
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) float Food = 80.f;
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) float Water = 80.f;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Dead, SaveGame) bool bDead = false;
	UPROPERTY(BlueprintReadOnly, Replicated, SaveGame) TArray<FTharrnActiveEffect> Effects;

	UPROPERTY(BlueprintAssignable) FTharrnDied OnDied;
	UPROPERTY(BlueprintAssignable) FTharrnDamaged OnDamaged;

	void InitPools(float InMaxHealth, float InMaxStamina, bool bInHasNeeds);

	/** Returns the damage actually applied. Kills at zero. */
	float ApplyDamage(float Amount, AActor* Instigator);
	void Heal(float Amount);
	/** Spends stamina for a discrete action; fails without spending if there is not enough. */
	bool TrySpendStamina(float Amount);
	/** Continuous drain (sprinting, absorbing a blocked hit). Clamps at zero. */
	void DrainStamina(float Amount);
	/** Applies a food item's restoration and effect. */
	void Consume(const FTharrnItemDef& Item);
	void AddWater(float Amount);
	void AddEffect(FName EffectId);
	bool HasEffect(FName EffectId) const;
	/** Clears death and restores pools to the recovery minimums. */
	void Recover(float HealthFraction, float MinFood, float MinWater);

	/** Scales stamina regeneration (blocking holds it down). */
	void SetRegenScale(float Scale) { RegenScale = Scale; }

	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Dead();

	void TickNeeds(float Seconds);
	bool HasAuthority() const;

	float LastStaminaSpendTime = -100.f;
	float NeedsAccumulator = 0.f;
	float RegenScale = 1.f;
};
