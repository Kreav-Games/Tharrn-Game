#pragma once

#include "CoreMinimal.h"
#include "Core/TharrnTypes.h"
#include "GameFramework/Character.h"
#include "TharrnCharacterBase.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTharrnCombatComponent;
class UTharrnInventoryComponent;
class UTharrnVitalsComponent;

UENUM(BlueprintType)
enum class ETharrnFaction : uint8
{
	/** Players, recruits and handled animals. */
	Expedition,
	/** Neutral animals. */
	Wildlife,
	/** Attacks the expedition on sight. */
	Hostile
};

/**
 * Shared base for every character: one vitals, combat and inventory model whether it is the player,
 * a creature, a recruit or a pack animal. Greybox bodies are built from engine basic shapes.
 */
UCLASS(Abstract)
class THARRN_API ATharrnCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	ATharrnCharacterBase();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tharrn") TObjectPtr<UTharrnVitalsComponent> Vitals;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tharrn") TObjectPtr<UTharrnCombatComponent> Combat;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tharrn") TObjectPtr<UTharrnInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, Category = "Tharrn|Body") TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere, Category = "Tharrn|Body") TObjectPtr<UStaticMeshComponent> HeadMesh;
	UPROPERTY(VisibleAnywhere, Category = "Tharrn|Body") TObjectPtr<USceneComponent> WeaponPivot;
	UPROPERTY(VisibleAnywhere, Category = "Tharrn|Body") TObjectPtr<UStaticMeshComponent> WeaponMesh;
	/** Ground disc shown during an attack windup so the threat area reads before the hit. */
	UPROPERTY(VisibleAnywhere, Category = "Tharrn|Body") TObjectPtr<UStaticMeshComponent> TelegraphMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tharrn") ETharrnFaction Faction = ETharrnFaction::Expedition;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tharrn") float WalkSpeed = 380.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tharrn") float RunSpeed = 650.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tharrn") bool bShowTelegraph = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tharrn") FLinearColor BaseColor = FLinearColor(0.2f, 0.35f, 0.6f);
	UPROPERTY(BlueprintReadOnly, Replicated, Category = "Tharrn") bool bWantsSprint = false;

	virtual bool IsHostileTo(const ATharrnCharacterBase* Other) const;
	virtual FTharrnMeleeStats GetAttackStats() const;
	virtual FName GetActiveToolTag() const { return NAME_None; }
	virtual void WearActiveTool(float Amount) {}
	virtual FString GetDisplayName() const;
	float GetBodyRadius() const;
	bool IsDead() const;

	/** Readable hit feedback on every machine (floating text, flash). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCombatEvent(ETharrnHitOutcome Outcome, float Damage);

	ETharrnHitOutcome LastEventOutcome = ETharrnHitOutcome::None;
	float LastEventDamage = 0.f;
	float LastEventTime = -100.f;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	virtual void HandleDeath(AActor* Killer);

	virtual float ComputeMoveSpeed() const;
	virtual void UpdateVisuals(float DeltaSeconds);
	void SetBodyColor(const FLinearColor& Color);

	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HeadMaterial;
	FLinearColor ShownColor = FLinearColor::Black;
	float WeaponPitch = 0.f;
};
