#include "Inventory/TharrnInventoryComponent.h"

#include "Core/TharrnData.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Tharrn.h"

UTharrnInventoryComponent::UTharrnInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UTharrnInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTharrnInventoryComponent, MaxSlots);
	DOREPLIFETIME(UTharrnInventoryComponent, MaxWeight);
	DOREPLIFETIME(UTharrnInventoryComponent, Slots);
}

bool UTharrnInventoryComponent::CanMutate() const
{
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

void UTharrnInventoryComponent::EnsureSlotArray()
{
	if (Slots.Num() < MaxSlots)
	{
		Slots.SetNum(MaxSlots);
	}
}

void UTharrnInventoryComponent::NotifyChanged()
{
	OnChanged.Broadcast();
}

void UTharrnInventoryComponent::OnRep_Slots()
{
	NotifyChanged();
}

int32 UTharrnInventoryComponent::CountItem(FName ItemId) const
{
	int32 Total = 0;
	for (const FTharrnItemStack& Stack : Slots)
	{
		if (!Stack.IsEmpty() && Stack.ItemId == ItemId)
		{
			Total += Stack.Count;
		}
	}
	return Total;
}

int32 UTharrnInventoryComponent::CountReserved(FName ItemId) const
{
	int32 Total = 0;
	for (const FTharrnReservation& Reservation : Reservations)
	{
		if (Reservation.ItemId == ItemId)
		{
			Total += Reservation.Count;
		}
	}
	return Total;
}

float UTharrnInventoryComponent::GetTotalWeight() const
{
	const FTharrnData& Data = FTharrnData::Get();
	float Total = 0.f;
	for (const FTharrnItemStack& Stack : Slots)
	{
		if (!Stack.IsEmpty())
		{
			Total += Data.GetItemWeight(Stack.ItemId) * Stack.Count;
		}
	}
	return Total;
}

float UTharrnInventoryComponent::GetWeightRatio() const
{
	return MaxWeight > 0.f ? GetTotalWeight() / MaxWeight : 0.f;
}

int32 UTharrnInventoryComponent::GetUsedSlots() const
{
	int32 Used = 0;
	for (const FTharrnItemStack& Stack : Slots)
	{
		Used += Stack.IsEmpty() ? 0 : 1;
	}
	return Used;
}

int32 UTharrnInventoryComponent::TotalItemCount() const
{
	int32 Total = 0;
	for (const FTharrnItemStack& Stack : Slots)
	{
		Total += Stack.IsEmpty() ? 0 : Stack.Count;
	}
	return Total;
}

int32 UTharrnInventoryComponent::ComputeAcceptable(FName ItemId, int32 Count) const
{
	const FTharrnItemDef* Def = FTharrnData::Get().FindItem(ItemId);
	if (!Def || Count <= 0)
	{
		return 0;
	}
	const int32 MaxStack = Def->EffectiveMaxStack();
	int64 Room = static_cast<int64>(FMath::Max(0, MaxSlots - Slots.Num())) * MaxStack;
	for (int32 Index = 0; Index < FMath::Min(Slots.Num(), MaxSlots); ++Index)
	{
		const FTharrnItemStack& Stack = Slots[Index];
		if (Stack.IsEmpty())
		{
			Room += MaxStack;
		}
		else if (Stack.ItemId == ItemId && !Def->UsesDurability())
		{
			Room += FMath::Max(0, MaxStack - Stack.Count);
		}
	}
	int32 Accept = static_cast<int32>(FMath::Min<int64>(Count, Room));
	if (MaxWeight > 0.f && Def->Weight > 0.f)
	{
		const float Free = MaxWeight * HardWeightRatio - GetTotalWeight();
		const int32 ByWeight = Free <= 0.f ? 0 : FMath::FloorToInt((Free + KINDA_SMALL_NUMBER) / Def->Weight);
		Accept = FMath::Min(Accept, ByWeight);
	}
	return FMath::Max(0, Accept);
}

bool UTharrnInventoryComponent::CanAcceptAll(const TArray<FTharrnItemAmount>& Amounts) const
{
	// Checked on a scratch copy so mixed amounts compete for the same free slots and weight.
	UTharrnInventoryComponent* Scratch = NewObject<UTharrnInventoryComponent>(GetTransientPackage());
	Scratch->MaxSlots = MaxSlots;
	Scratch->MaxWeight = MaxWeight;
	Scratch->HardWeightRatio = HardWeightRatio;
	Scratch->Slots = Slots;
	for (const FTharrnItemAmount& Amount : Amounts)
	{
		if (Amount.Count > 0 && Scratch->AddItem(Amount.ItemId, Amount.Count) < Amount.Count)
		{
			return false;
		}
	}
	return true;
}

int32 UTharrnInventoryComponent::AddItem(FName ItemId, int32 Count, float Durability)
{
	if (!CanMutate())
	{
		return 0;
	}
	const FTharrnItemDef* Def = FTharrnData::Get().FindItem(ItemId);
	const int32 Accept = ComputeAcceptable(ItemId, Count);
	if (!Def || Accept <= 0)
	{
		return 0;
	}
	EnsureSlotArray();
	const int32 MaxStack = Def->EffectiveMaxStack();
	int32 Left = Accept;
	if (!Def->UsesDurability())
	{
		for (FTharrnItemStack& Stack : Slots)
		{
			if (Left > 0 && !Stack.IsEmpty() && Stack.ItemId == ItemId && Stack.Count < MaxStack)
			{
				const int32 Moved = FMath::Min(Left, MaxStack - Stack.Count);
				Stack.Count += Moved;
				Left -= Moved;
			}
		}
	}
	for (int32 Index = 0; Index < MaxSlots && Left > 0; ++Index)
	{
		FTharrnItemStack& Stack = Slots[Index];
		if (Stack.IsEmpty())
		{
			const int32 Moved = FMath::Min(Left, MaxStack);
			Stack.ItemId = ItemId;
			Stack.Count = Moved;
			Stack.Durability = Def->UsesDurability() ? (Durability >= 0.f ? Durability : Def->MaxDurability) : -1.f;
			Left -= Moved;
		}
	}
	ensureMsgf(Left == 0, TEXT("Inventory add planned %d of %s but %d did not fit"), Accept, *ItemId.ToString(), Left);
	NotifyChanged();
	return Accept - Left;
}

int32 UTharrnInventoryComponent::RemoveItem(FName ItemId, int32 Count)
{
	if (!CanMutate() || Count <= 0)
	{
		return 0;
	}
	int32 Left = FMath::Min(Count, CountAvailable(ItemId));
	const int32 Planned = Left;
	for (int32 Index = Slots.Num() - 1; Index >= 0 && Left > 0; --Index)
	{
		FTharrnItemStack& Stack = Slots[Index];
		if (!Stack.IsEmpty() && Stack.ItemId == ItemId)
		{
			const int32 Moved = FMath::Min(Left, Stack.Count);
			Stack.Count -= Moved;
			Left -= Moved;
			if (Stack.Count <= 0)
			{
				Stack = FTharrnItemStack();
			}
		}
	}
	if (Planned > 0)
	{
		NotifyChanged();
	}
	return Planned - Left;
}

int32 UTharrnInventoryComponent::RemoveFromSlot(int32 SlotIndex, int32 Count, FTharrnItemStack& OutRemoved)
{
	OutRemoved = FTharrnItemStack();
	if (!CanMutate() || !Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty() || Count <= 0)
	{
		return 0;
	}
	FTharrnItemStack& Stack = Slots[SlotIndex];
	const int32 Moved = FMath::Min3(Count, Stack.Count, CountAvailable(Stack.ItemId));
	if (Moved <= 0)
	{
		return 0;
	}
	OutRemoved.ItemId = Stack.ItemId;
	OutRemoved.Count = Moved;
	OutRemoved.Durability = Stack.Durability;
	Stack.Count -= Moved;
	if (Stack.Count <= 0)
	{
		Stack = FTharrnItemStack();
	}
	NotifyChanged();
	return Moved;
}

bool UTharrnInventoryComponent::SetSlotDurability(int32 SlotIndex, float Durability)
{
	if (!CanMutate() || !Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].IsEmpty() || Slots[SlotIndex].Durability < 0.f)
	{
		return false;
	}
	Slots[SlotIndex].Durability = FMath::Max(0.f, Durability);
	NotifyChanged();
	return true;
}

void UTharrnInventoryComponent::ResetContents()
{
	if (!CanMutate())
	{
		return;
	}
	Slots.Reset();
	EnsureSlotArray();
	Reservations.Reset();
	NotifyChanged();
}

int32 UTharrnInventoryComponent::Reserve(FName ItemId, int32 Count, const FGuid& Holder)
{
	if (!CanMutate() || Count <= 0 || CountAvailable(ItemId) < Count)
	{
		return 0;
	}
	FTharrnReservation& Reservation = Reservations.AddDefaulted_GetRef();
	Reservation.Handle = NextReservationHandle++;
	Reservation.ItemId = ItemId;
	Reservation.Count = Count;
	Reservation.Holder = Holder;
	return Reservation.Handle;
}

bool UTharrnInventoryComponent::ConsumeReservation(int32 Handle)
{
	const int32 Index = Reservations.IndexOfByPredicate([Handle](const FTharrnReservation& R) { return R.Handle == Handle; });
	if (!CanMutate() || Index == INDEX_NONE)
	{
		return false;
	}
	const FTharrnReservation Reservation = Reservations[Index];
	Reservations.RemoveAt(Index);
	const int32 Removed = RemoveItem(Reservation.ItemId, Reservation.Count);
	ensureMsgf(Removed == Reservation.Count, TEXT("Reservation %d of %s consumed %d"), Reservation.Count, *Reservation.ItemId.ToString(), Removed);
	return Removed == Reservation.Count;
}

void UTharrnInventoryComponent::ReleaseReservation(int32 Handle)
{
	Reservations.RemoveAll([Handle](const FTharrnReservation& R) { return R.Handle == Handle; });
}

int32 UTharrnInventoryComponent::ReleaseAllFor(const FGuid& Holder)
{
	return Reservations.RemoveAll([&Holder](const FTharrnReservation& R) { return R.Holder == Holder; });
}

int32 UTharrnInventoryComponent::TransferSlot(UTharrnInventoryComponent* From, int32 SlotIndex, int32 Count, UTharrnInventoryComponent* To)
{
	if (!From || !To || From == To || !From->CanMutate() || !To->CanMutate())
	{
		return 0;
	}
	const FTharrnItemStack* Source = From->GetSlot(SlotIndex);
	if (!Source || Source->IsEmpty())
	{
		return 0;
	}
	const FName ItemId = Source->ItemId;
	int32 Want = Count <= 0 ? Source->Count : FMath::Min(Count, Source->Count);
	Want = FMath::Min(Want, From->CountAvailable(ItemId));
	Want = FMath::Min(Want, To->ComputeAcceptable(ItemId, Want));
	if (Want <= 0)
	{
		return 0;
	}
	FTharrnItemStack Removed;
	const int32 Taken = From->RemoveFromSlot(SlotIndex, Want, Removed);
	const int32 Added = To->AddItem(Removed.ItemId, Taken, Removed.Durability);
	if (Added < Taken)
	{
		// Never expected (acceptance was computed first); put the remainder back rather than lose it.
		const int32 Returned = From->AddItem(Removed.ItemId, Taken - Added, Removed.Durability);
		UE_LOG(LogTharrn, Error, TEXT("Transfer of %s fell short: %d taken, %d added, %d returned"), *ItemId.ToString(), Taken, Added, Returned);
	}
	return Added;
}

int32 UTharrnInventoryComponent::TransferItem(UTharrnInventoryComponent* From, FName ItemId, int32 Count, UTharrnInventoryComponent* To)
{
	if (!From)
	{
		return 0;
	}
	int32 Moved = 0;
	for (int32 Index = 0; Index < From->Slots.Num() && Moved < Count; ++Index)
	{
		if (!From->Slots[Index].IsEmpty() && From->Slots[Index].ItemId == ItemId)
		{
			const int32 Step = TransferSlot(From, Index, Count - Moved, To);
			if (Step <= 0)
			{
				break;
			}
			Moved += Step;
		}
	}
	return Moved;
}

int32 UTharrnInventoryComponent::TransferAll(UTharrnInventoryComponent* From, UTharrnInventoryComponent* To)
{
	if (!From)
	{
		return 0;
	}
	int32 Moved = 0;
	for (int32 Index = 0; Index < From->Slots.Num(); ++Index)
	{
		if (!From->Slots[Index].IsEmpty())
		{
			Moved += TransferSlot(From, Index, 0, To);
		}
	}
	return Moved;
}
