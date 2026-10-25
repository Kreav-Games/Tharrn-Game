#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TharrnTypes.h"
#include "TharrnCombatComponent.generated.h"

UENUM(BlueprintType)
enum class ETharrnAttackPhase : uint8
{
	None,
	Windup,
	Active,
	Recovery
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FTharrnLandedHit, AActor* /*Victim*/, ETharrnHitOutcome);

/**
 * Melee for players and creatures alike. An attack runs Windup (telegraph), Active (hits resolve, once per target)
 * and Recovery (punish window). Blocking costs stamina per hit; a block raised just in time parries and staggers
 * the attacker; a dodge grants brief invulnerability. All resolution happens on the server.
 */
UCLASS(ClassGroup = (Tharrn), meta = (BlueprintSpawnableComponent))
class THARRN_API UTharrnCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTharrnCombatComponent();

	UPROPERTY(BlueprintReadOnly, Replicated) ETharrnAttackPhase Phase = ETharrnAttackPhase::None;
	UPROPERTY(BlueprintReadOnly, Replicated) float PhaseStartTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Replicated) float PhaseDuration = 0.f;
	UPROPERTY(BlueprintReadOnly, Replicated) bool bBlocking = false;
	UPROPERTY(BlueprintReadOnly, Replicated) bool bStaggered = false;
	UPROPERTY(BlueprintReadOnly, Replicated) bool bDodging = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxPoise = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PoiseRegenPerSecond = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ParryWindow = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlockDamageReduction = 0.75f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlockStaminaPerDamage = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeStaminaCost = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeInvulnerability = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DodgeSpeed = 1100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaggerSeconds = 0.7f;

	FTharrnLandedHit OnLandedHit;

	bool TryStartAttack(const FTharrnMeleeStats& Stats);
	void CancelAttack();
	void SetBlocking(bool bNewBlocking);
	bool TryDodge(const FVector& Direction);
	void ApplyStagger(float Seconds, const FVector& PushDirection);

	bool CanAct() const;
	bool IsInvulnerable() const;
	bool IsAttacking() const { return Phase != ETharrnAttackPhase::None; }
	float GetPhaseAlpha() const;
	float GetPoise() const { return Poise; }
	float GetBlockStartTime() const { return BlockStartTime; }
	const FTharrnMeleeStats& GetCurrentAttack() const { return CurrentAttack; }
	int32 GetSwingCount() const { return SwingCount; }

	/** Resolves one landed attack against Victim and applies the result. Server-only. */
	static ETharrnHitOutcome ResolveHit(AActor* Attacker, AActor* Victim, const FTharrnMeleeStats& Stats, float& OutDamage);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void EnterPhase(ETharrnAttackPhase NewPhase, float Duration);
	void DetectHits();
	float Now() const;

	FTharrnMeleeStats CurrentAttack;
	TArray<TWeakObjectPtr<AActor>> HitThisSwing;
	float PhaseEndTime = 0.f;
	float Poise = 40.f;
	float BlockStartTime = -100.f;
	float StaggerEndTime = 0.f;
	float InvulnerableUntil = 0.f;
	float DodgeEndTime = 0.f;
	int32 SwingCount = 0;
};
