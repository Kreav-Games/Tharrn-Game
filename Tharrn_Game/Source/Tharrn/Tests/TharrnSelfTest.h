#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TharrnSelfTest.generated.h"

class ATharrnCreature;
class ATharrnDistrictBuilder;
class ATharrnPlayerCharacter;

/**
 * In-game acceptance run for a milestone, against the real district, player, AI and save code.
 * Start the game with -TharrnSelfTest=M1 [-TharrnFresh -TharrnProfile=selftest]; results go to
 * Saved/TharrnSelfTest/<Mode>.json and the log, screenshots beside them when rendering is available.
 */
UCLASS()
class THARRN_API UTharrnSelfTest : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

protected:
	struct FStep
	{
		FString Name;
		float Timeout = 20.f;
		TFunction<bool(float)> Run;
	};
	struct FCheck
	{
		FString Step;
		FString Name;
		bool bPass = false;
		FString Detail;
	};

	void BuildM1();
	void AddStep(const FString& Name, float Timeout, TFunction<bool(float)> Run);
	bool Check(bool bCondition, const FString& Name, const FString& Detail = FString());
	/** A precondition evaluated every tick: records only a failure. */
	bool Require(bool bCondition, const FString& Name);
	void Finish();
	bool Shot(const FString& Name, const FString& Caption, float Elapsed, float& ShotTime);

	ATharrnPlayerCharacter* Player() const;
	ATharrnDistrictBuilder* Builder() const;
	ATharrnCreature* FirstCreature() const;
	void PlaceNear(AActor* Target, float Distance, float ApproachYaw = 0.f);
	void FaceTowards(const FVector& Location, float Pitch = -12.f);
	void SetAllBrains(bool bEnabled);
	int32 SlotOf(FName ItemId) const;
	TMap<FName, int32> CountsOf(const class UTharrnInventoryComponent* Inventory) const;
	FString Describe(const TMap<FName, int32>& Counts) const;

	TArray<FStep> Steps;
	TArray<FCheck> Checks;
	FString Mode;
	int32 Current = 0;
	float StepStart = 0.f;
	int32 Phase = 0;
	float Mark = 0.f;
	float Number = 0.f;
	float Number2 = 0.f;
	TMap<FName, int32> Snapshot;
	TMap<FName, int32> Snapshot2;
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<AActor> Target2;
	TArray<FString> Notes;
	bool bActive = false;
	bool bCanShoot = false;
	bool bShotPending = false;
	float ShotRequestedAt = 0.f;
	int32 Counter = 0;
	FVector RoomCenter(const FString& Key) const;
};
