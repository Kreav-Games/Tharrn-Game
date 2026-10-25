#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/TharrnTypes.h"
#include "TharrnInventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTharrnInventoryChanged);

/**
 * The one inventory model: players, containers, loot caches and (later) workers, animal packs and gate cargo.
 * Every mutation is server-only and returns the quantity actually moved, so callers can never create or lose items
 * by assuming a full transfer. Reserved quantities cannot be removed except by consuming the reservation.
 */
UCLASS(ClassGroup = (Tharrn), meta = (BlueprintSpawnableComponent))
class THARRN_API UTharrnInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTharrnInventoryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, SaveGame, Category = "Inventory")
	int32 MaxSlots = 24;

	/** Carry weight before the owner is overburdened. 0 means weight is not limited (containers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Inventory")
	float MaxWeight = 0.f;

	/** Nothing more can be added above MaxWeight * HardWeightRatio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	float HardWeightRatio = 1.5f;

	UPROPERTY(BlueprintAssignable)
	FTharrnInventoryChanged OnChanged;

	const TArray<FTharrnItemStack>& GetSlots() const { return Slots; }
	const FTharrnItemStack* GetSlot(int32 Index) const { return Slots.IsValidIndex(Index) ? &Slots[Index] : nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Inventory") int32 CountItem(FName ItemId) const;
	UFUNCTION(BlueprintCallable, Category = "Inventory") int32 CountReserved(FName ItemId) const;
	UFUNCTION(BlueprintCallable, Category = "Inventory") int32 CountAvailable(FName ItemId) const { return CountItem(ItemId) - CountReserved(ItemId); }
	UFUNCTION(BlueprintCallable, Category = "Inventory") float GetTotalWeight() const;
	/** Weight / MaxWeight, or 0 when weight is unlimited. */
	UFUNCTION(BlueprintCallable, Category = "Inventory") float GetWeightRatio() const;
	UFUNCTION(BlueprintCallable, Category = "Inventory") int32 GetUsedSlots() const;
	UFUNCTION(BlueprintCallable, Category = "Inventory") int32 TotalItemCount() const;
	bool IsEmpty() const { return TotalItemCount() == 0; }

	/** How many of ItemId could be added now, limited by slots, stacks and the hard weight cap. */
	int32 ComputeAcceptable(FName ItemId, int32 Count) const;
	/** True if every amount fits at once. */
	bool CanAcceptAll(const TArray<FTharrnItemAmount>& Amounts) const;

	int32 AddItem(FName ItemId, int32 Count, float Durability = -1.f);
	int32 RemoveItem(FName ItemId, int32 Count);
	int32 RemoveFromSlot(int32 SlotIndex, int32 Count, FTharrnItemStack& OutRemoved);
	bool SetSlotDurability(int32 SlotIndex, float Durability);
	/** Removes everything and every reservation. Used by load and tests only. */
	void ResetContents();

	int32 Reserve(FName ItemId, int32 Count, const FGuid& Holder);
	bool ConsumeReservation(int32 Handle);
	void ReleaseReservation(int32 Handle);
	int32 ReleaseAllFor(const FGuid& Holder);
	const TArray<FTharrnReservation>& GetReservations() const { return Reservations; }

	static int32 TransferSlot(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To);
	static int32 TransferItem(UTharrnInventoryComponent* From, FName ItemId, int32 Count, UTharrnInventoryComponent* To);
	static int32 TransferAll(UTharrnInventoryComponent* From, UTharrnInventoryComponent* To);

	bool CanMutate() const;
	void NotifyChanged();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Slots, SaveGame)
	TArray<FTharrnItemStack> Slots;

	UPROPERTY(Transient)
	TArray<FTharrnReservation> Reservations;

	int32 NextReservationHandle = 1;

	UFUNCTION()
	void OnRep_Slots();

	void EnsureSlotArray();
};
