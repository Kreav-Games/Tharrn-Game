#pragma once

#include "CoreMinimal.h"
#include "TharrnTypes.generated.h"

// Gameplay definitions are loaded from Content/TharrnData/*.json (see FTharrnData).
// Field names in the JSON match the UPROPERTY names below.

UENUM(BlueprintType)
enum class ETharrnItemCategory : uint8
{
	Resource,
	Food,
	Tool,
	Weapon,
	Component,
	Misc
};

UENUM(BlueprintType)
enum class ETharrnHitOutcome : uint8
{
	None,
	Hit,
	Staggered,
	Blocked,
	GuardBroken,
	Parried,
	Dodged
};

/** One melee action. Windup is the readable telegraph before the hit becomes live. */
USTRUCT(BlueprintType)
struct FTharrnMeleeStats
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Damage = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaminaCost = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Windup = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Active = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Recovery = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Range = 140.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ArcDegrees = 110.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Poise = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Lunge = 0.f;
};

USTRUCT(BlueprintType)
struct FTharrnItemAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame) FName ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame) int32 Count = 0;
};

/** A stack in an inventory slot. Items with durability never stack. */
USTRUCT(BlueprintType)
struct FTharrnItemStack
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, SaveGame) FName ItemId;
	UPROPERTY(BlueprintReadOnly, SaveGame) int32 Count = 0;
	UPROPERTY(BlueprintReadOnly, SaveGame) float Durability = -1.f;

	bool IsEmpty() const { return ItemId.IsNone() || Count <= 0; }
	bool operator==(const FTharrnItemStack& Other) const
	{
		return ItemId == Other.ItemId && Count == Other.Count && FMath::IsNearlyEqual(Durability, Other.Durability);
	}
};

/** Inputs set aside for a holder (a worker task, a workstation job). Server-only and never saved. */
USTRUCT()
struct FTharrnReservation
{
	GENERATED_BODY()

	UPROPERTY() int32 Handle = 0;
	UPROPERTY() FName ItemId;
	UPROPERTY() int32 Count = 0;
	UPROPERTY() FGuid Holder;
};

USTRUCT(BlueprintType)
struct FTharrnItemDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) ETharrnItemCategory Category = ETharrnItemCategory::Resource;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 MaxStack = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Weight = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Food = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Water = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Health = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Effect;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ToolTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxDurability = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bHasMelee = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FTharrnMeleeStats Melee;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor(0.6f, 0.6f, 0.6f);

	bool UsesDurability() const { return MaxDurability > 0.f; }
	int32 EffectiveMaxStack() const { return UsesDurability() ? 1 : FMath::Max(1, MaxStack); }
};

USTRUCT(BlueprintType)
struct FTharrnEffectDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Duration = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaminaRegenMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealthRegenPerSecond = 0.f;
};

USTRUCT(BlueprintType)
struct FTharrnResourceDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	/** Empty means it can be gathered by hand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName RequiredToolTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnItemAmount> Yields;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Charges = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float GatherSeconds = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RespawnSeconds = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ToolWear = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Shape = TEXT("Cube");
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Size = FVector(1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);
};

USTRUCT(BlueprintType)
struct FTharrnContainerDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Slots = 12;
	/** Granted once, the first time the container exists in a campaign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnItemAmount> InitialContents;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bDestroyWhenEmpty = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Size = FVector(0.8f, 0.6f, 0.6f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor(0.45f, 0.3f, 0.15f);
};

USTRUCT(BlueprintType)
struct FTharrnCreatureDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bHostile = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxHealth = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float MaxPoise = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WalkSpeed = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RunSpeed = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AggroRadius = 1400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LeashRadius = 3000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WanderRadius = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float AttackCooldown = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FTharrnMeleeStats Attack;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnItemAmount> Drops;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RespawnSeconds = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector BodyScale = FVector(1.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor(0.5f, 0.1f, 0.1f);
};

/** Survival rates. Times are in seconds of play. */
USTRUCT(BlueprintType)
struct FTharrnSurvivalTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FoodDrainPerSecond = 0.045f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaterDrainPerSecond = 0.07f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StarvingHealthLossPerSecond = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DehydratedHealthLossPerSecond = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealthRegenPerSecond = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float HealthRegenNeedsThreshold = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaminaRegenPerSecond = 18.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float StaminaRegenDelay = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float SprintStaminaPerSecond = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float BlockingStaminaRegenScale = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WaterPerDrink = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float DrinkSeconds = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RecoverySeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RecoveryHealthFraction = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RecoveryMinFood = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float RecoveryMinWater = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float PlayerCarryWeight = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 PlayerSlots = 24;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FTharrnMeleeStats Unarmed;
};

// District layout (greybox). Rooms are axis-aligned boxes; walls are generated with openings where rooms touch.

USTRUCT(BlueprintType)
struct FTharrnDistrictRoom
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D Min = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D Max = FVector2D(1000.f, 1000.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FloorZ = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Height = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Floor = TEXT("floor");
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Wall = TEXT("rock");
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCeiling = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LightIntensity = 8000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor LightColor = FLinearColor(1.f, 0.85f, 0.7f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float LabelYaw = 180.f;
};

USTRUCT(BlueprintType)
struct FTharrnDistrictBlock
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Size = FVector(100.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Yaw = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Material = TEXT("rock");
};

USTRUCT(BlueprintType)
struct FTharrnDistrictSpawn
{
	GENERATED_BODY()

	/** Resource, Container, Water, Spawner, Label. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Type;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Def;
	/** Stable identifier; persistence keys on it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Yaw = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Label;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Count = 1;
};

USTRUCT(BlueprintType)
struct FTharrnDistrictLight
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Intensity = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float Radius = 2500.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor(1.f, 0.85f, 0.7f);
};

USTRUCT(BlueprintType)
struct FTharrnPaletteEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor Color = FLinearColor::Gray;
};

USTRUCT(BlueprintType)
struct FTharrnDistrictLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnPaletteEntry> Palette;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnDistrictRoom> Rooms;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnDistrictBlock> Blocks;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnDistrictLight> Lights;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FTharrnDistrictSpawn> Spawns;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector ArrivalPoint = FVector(0.f, 0.f, 120.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ArrivalYaw = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector RecoveryPoint = FVector(0.f, 0.f, 120.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float WallThickness = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float FillLightIntensity = 0.6f;
	/** Exposure compensation in stops (negative = darker). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float ExposureBias = -1.f;
};
