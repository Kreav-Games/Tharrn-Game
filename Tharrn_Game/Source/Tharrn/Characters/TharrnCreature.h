#pragma once

#include "CoreMinimal.h"
#include "Characters/TharrnCharacterBase.h"
#include "Core/TharrnTypes.h"
#include "TharrnCreature.generated.h"

class ATharrnCreatureSpawner;

UENUM(BlueprintType)
enum class ETharrnCreatureState : uint8
{
	Idle,
	Wander,
	Chase,
	Return,
	Dead
};

/**
 * Wildlife and hostile creatures, configured from creatures.json. The brain runs on the server at a fixed think rate
 * (not every frame): acquire the nearest expedition member in aggro range, chase, telegraph an attack, recover, and
 * give up past the leash radius.
 */
UCLASS()
class THARRN_API ATharrnCreature : public ATharrnCharacterBase
{
	GENERATED_BODY()

public:
	ATharrnCreature();

	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Def) FName CreatureDefId;
	UPROPERTY(BlueprintReadOnly, Replicated) ETharrnCreatureState State = ETharrnCreatureState::Idle;

	FTharrnCreatureDef Def;
	TWeakObjectPtr<ATharrnCreatureSpawner> Spawner;
	FVector HomeLocation = FVector::ZeroVector;

	/** Tests freeze the brain to place the creature deliberately. */
	void SetBrainEnabled(bool bEnabled);
	ATharrnCharacterBase* GetTarget() const { return Target.Get(); }
	int32 GetTelegraphCount() const { return TelegraphCount; }
	bool UsedDirectChase() const { return bDirectChaseUsed; }
	bool HasNavigatedPath() const { return bPathSucceeded; }

	virtual FTharrnMeleeStats GetAttackStats() const override { return Def.Attack; }
	virtual FString GetDisplayName() const override { return Def.Name.IsEmpty() ? CreatureDefId.ToString() : Def.Name; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Def();
	void ApplyDef();
	void Think();
	ATharrnCharacterBase* FindTarget() const;
	void MoveTowards(const FVector& Location, AActor* Goal, float Acceptance);
	virtual void HandleDeath(AActor* Killer) override;

	TWeakObjectPtr<ATharrnCharacterBase> Target;
	bool bBrainEnabled = true;
	bool bApplied = false;
	bool bDirectChase = false;
	bool bDirectChaseUsed = false;
	bool bPathSucceeded = false;
	float NextThinkTime = 0.f;
	float NextAttackTime = 0.f;
	float NextWanderTime = 0.f;
	int32 TelegraphCount = 0;
	FVector DirectGoal = FVector::ZeroVector;
};
