#pragma once

#include "CoreMinimal.h"
#include "Core/TharrnTypes.h"
#include "GameFramework/Actor.h"
#include "TharrnDistrictBuilder.generated.h"

class UInstancedStaticMeshComponent;

/**
 * Builds the greybox district from Content/TharrnData/<LayoutFile>: floors, walls with openings where rooms meet,
 * ceilings, props, lights and zone labels on every machine, and the gameplay actors (nodes, containers, water,
 * creature spawners) on the server with stable ids derived from the layout. Edit the JSON, not the map, to reshape it.
 */
UCLASS()
class THARRN_API ATharrnDistrictBuilder : public AActor
{
	GENERATED_BODY()

public:
	ATharrnDistrictBuilder();

	static constexpr int32 PaletteSize = 10;

	UPROPERTY(EditAnywhere, Category = "District") FString LayoutFile = TEXT("district_slice.json");
	UPROPERTY(VisibleAnywhere, Category = "District") TArray<TObjectPtr<UInstancedStaticMeshComponent>> PaletteMeshes;

	static ATharrnDistrictBuilder* Find(const UWorld* World);

	const FTharrnDistrictLayout& GetLayout() const { return Layout; }
	FVector GetArrivalPoint() const { return GetActorTransform().TransformPosition(Layout.ArrivalPoint); }
	FVector GetRecoveryPoint() const { return GetActorTransform().TransformPosition(Layout.RecoveryPoint); }
	float GetArrivalYaw() const { return Layout.ArrivalYaw; }
	bool HasSpawnedGameplay() const { return bGameplaySpawned; }
	/** Server: the actor spawned for a layout id ("cont.arrival.crate"). */
	AActor* FindSpawned(const FString& Id) const;
	TArray<AActor*> FindSpawnedOfDef(FName Def) const;
	int32 GetBlockCount() const { return BlockCount; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;

protected:
	bool LoadLayout();
	void BuildGeometry();
	void AddBlock(const FVector& Center, const FVector& Size, float Yaw, FName Material);
	int32 PaletteIndex(FName Material) const;
	void BuildRoomWalls(int32 RoomIndex);
	void BuildLighting();
	void BuildLabels();
	void SpawnGameplay();

	UPROPERTY(Transient) FTharrnDistrictLayout Layout;
	TMap<FString, TWeakObjectPtr<AActor>> Spawned;
	TMap<FString, FName> SpawnedDefs;
	bool bLayoutLoaded = false;
	bool bGameplaySpawned = false;
	int32 BlockCount = 0;
};
