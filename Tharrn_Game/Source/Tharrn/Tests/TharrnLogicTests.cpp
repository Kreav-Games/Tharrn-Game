// Automation tests for world-free logic: data, inventory conservation, reservations, layout integrity.
// Run: UnrealEditor-Cmd Tharrn.uproject -ExecCmds="Automation RunTests Tharrn; Quit" -unattended -nullrhi

#include "Core/TharrnData.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UTharrnInventoryComponent* MakeInventory(int32 Slots, float MaxWeight = 0.f)
	{
		UTharrnInventoryComponent* Inventory = NewObject<UTharrnInventoryComponent>(GetTransientPackage());
		Inventory->MaxSlots = Slots;
		Inventory->MaxWeight = MaxWeight;
		Inventory->ResetContents();
		return Inventory;
	}

	int32 Total(const UTharrnInventoryComponent* A, const UTharrnInventoryComponent* B, FName Item)
	{
		return A->CountItem(Item) + B->CountItem(Item);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnDataLoadsTest, "Tharrn.Data.LoadsWithoutErrors", TestFlags)
bool FTharrnDataLoadsTest::RunTest(const FString& Parameters)
{
	FTharrnData& Data = FTharrnData::Get();
	Data.Reload();
	for (const FString& Error : Data.GetErrors())
	{
		AddError(Error);
	}
	TestTrue(TEXT("stone item"), Data.FindItem(TEXT("stone")) != nullptr);
	TestTrue(TEXT("pick is a mining tool"), Data.FindItem(TEXT("pick")) && Data.FindItem(TEXT("pick"))->ToolTag == TEXT("mining"));
	TestTrue(TEXT("cleaver has melee"), Data.FindItem(TEXT("cleaver")) && Data.FindItem(TEXT("cleaver"))->bHasMelee);
	TestTrue(TEXT("skitter creature"), Data.FindCreature(TEXT("skitter")) != nullptr);
	TestTrue(TEXT("survival tuned"), Data.GetSurvival().FoodDrainPerSecond > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnInventoryStacksTest, "Tharrn.Inventory.StacksAndSlots", TestFlags)
bool FTharrnInventoryStacksTest::RunTest(const FString& Parameters)
{
	const int32 Stack = FTharrnData::Get().FindItem(TEXT("stone"))->EffectiveMaxStack();
	UTharrnInventoryComponent* Inv = MakeInventory(3);
	TestEqual(TEXT("first add fills stacks"), Inv->AddItem(TEXT("stone"), Stack * 2 + 5), Stack * 2 + 5);
	TestEqual(TEXT("second add stops at capacity"), Inv->AddItem(TEXT("stone"), Stack), Stack - 5);
	TestEqual(TEXT("count at capacity"), Inv->CountItem(TEXT("stone")), Stack * 3);
	TestEqual(TEXT("full inventory accepts nothing"), Inv->AddItem(TEXT("fibre"), 1), 0);
	TestEqual(TEXT("unknown items are refused"), Inv->AddItem(TEXT("not_an_item"), 1), 0);
	TestEqual(TEXT("remove more than held removes what is held"), Inv->RemoveItem(TEXT("stone"), Stack * 5), Stack * 3);
	TestTrue(TEXT("empty after removal"), Inv->IsEmpty());

	UTharrnInventoryComponent* Tools = MakeInventory(4);
	TestEqual(TEXT("durability items never stack"), Tools->AddItem(TEXT("pick"), 2), 2);
	TestEqual(TEXT("two picks use two slots"), Tools->GetUsedSlots(), 2);
	TestEqual(TEXT("new tool has full durability"), Tools->GetSlot(0)->Durability, FTharrnData::Get().FindItem(TEXT("pick"))->MaxDurability);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnInventoryWeightTest, "Tharrn.Inventory.WeightCap", TestFlags)
bool FTharrnInventoryWeightTest::RunTest(const FString& Parameters)
{
	const float StoneWeight = FTharrnData::Get().FindItem(TEXT("stone"))->Weight;
	UTharrnInventoryComponent* Inv = MakeInventory(10, StoneWeight * 10.f);
	const int32 Cap = FMath::FloorToInt(10.f * Inv->HardWeightRatio + KINDA_SMALL_NUMBER);
	TestEqual(TEXT("adds stop at the hard weight cap"), Inv->AddItem(TEXT("stone"), 100), Cap);
	TestTrue(TEXT("overburdened above the soft limit"), Inv->GetWeightRatio() > 1.f);
	TestEqual(TEXT("nothing heavy fits at the cap"), Inv->ComputeAcceptable(TEXT("stone"), 1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnInventoryConservationTest, "Tharrn.Inventory.TransfersConserveItems", TestFlags)
bool FTharrnInventoryConservationTest::RunTest(const FString& Parameters)
{
	const FName Items[] = {TEXT("stone"), TEXT("fibre"), TEXT("fungus"), TEXT("scrap_metal"), TEXT("pick")};
	UTharrnInventoryComponent* A = MakeInventory(6, 40.f);
	UTharrnInventoryComponent* B = MakeInventory(4);
	A->AddItem(TEXT("stone"), 30);
	A->AddItem(TEXT("fibre"), 60);
	A->AddItem(TEXT("pick"), 1);
	B->AddItem(TEXT("fungus"), 12);
	B->AddItem(TEXT("scrap_metal"), 20);
	TMap<FName, int32> Expected;
	for (const FName Item : Items)
	{
		Expected.Add(Item, Total(A, B, Item));
	}
	FRandomStream Random(1234);
	int32 Moved = 0;
	for (int32 Step = 0; Step < 2000; ++Step)
	{
		const bool bForward = Random.FRand() < 0.5f;
		UTharrnInventoryComponent* From = bForward ? A : B;
		UTharrnInventoryComponent* To = bForward ? B : A;
		const int32 Slot = Random.RandRange(0, From->MaxSlots - 1);
		const int32 Count = Random.RandRange(0, 40);
		if (Random.FRand() < 0.1f)
		{
			// Reservations must survive transfers untouched.
			From->Reserve(Items[Random.RandRange(0, 3)], Random.RandRange(1, 5), FGuid::NewGuid());
		}
		Moved += UTharrnInventoryComponent::TransferSlot(From, Slot, Count, To);
		if (Random.FRand() < 0.05f)
		{
			A->ReleaseAllFor(FGuid());
			for (const FTharrnReservation& R : TArray<FTharrnReservation>(A->GetReservations())) { A->ReleaseReservation(R.Handle); }
			for (const FTharrnReservation& R : TArray<FTharrnReservation>(B->GetReservations())) { B->ReleaseReservation(R.Handle); }
		}
		for (const FName Item : Items)
		{
			if (Total(A, B, Item) != Expected[Item])
			{
				AddError(FString::Printf(TEXT("Step %d: %s total %d, expected %d"), Step, *Item.ToString(), Total(A, B, Item), Expected[Item]));
				return false;
			}
			if (A->CountItem(Item) < A->CountReserved(Item) || B->CountItem(Item) < B->CountReserved(Item))
			{
				AddError(FString::Printf(TEXT("Step %d: reserved %s was moved away"), Step, *Item.ToString()));
				return false;
			}
		}
		if (A->MaxWeight > 0.f && A->GetTotalWeight() > A->MaxWeight * A->HardWeightRatio + 0.01f)
		{
			AddError(FString::Printf(TEXT("Step %d: weight cap exceeded"), Step));
			return false;
		}
	}
	TestTrue(TEXT("transfers actually moved items"), Moved > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnInventoryReservationTest, "Tharrn.Inventory.Reservations", TestFlags)
bool FTharrnInventoryReservationTest::RunTest(const FString& Parameters)
{
	UTharrnInventoryComponent* Inv = MakeInventory(4);
	UTharrnInventoryComponent* Other = MakeInventory(4);
	const FGuid WorkerA = FGuid::NewGuid();
	const FGuid WorkerB = FGuid::NewGuid();
	Inv->AddItem(TEXT("fibre"), 10);
	const int32 HandleA = Inv->Reserve(TEXT("fibre"), 6, WorkerA);
	TestTrue(TEXT("reserve within stock"), HandleA != 0);
	TestEqual(TEXT("available excludes reserved"), Inv->CountAvailable(TEXT("fibre")), 4);
	TestEqual(TEXT("second holder cannot over-reserve"), Inv->Reserve(TEXT("fibre"), 5, WorkerB), 0);
	TestEqual(TEXT("plain removal leaves the reserved part"), Inv->RemoveItem(TEXT("fibre"), 10), 4);
	TestEqual(TEXT("transfer leaves the reserved part"), UTharrnInventoryComponent::TransferItem(Inv, TEXT("fibre"), 10, Other), 0);
	TestTrue(TEXT("consume takes exactly the reservation"), Inv->ConsumeReservation(HandleA));
	TestEqual(TEXT("stock spent once"), Inv->CountItem(TEXT("fibre")), 0);
	TestFalse(TEXT("a reservation cannot be consumed twice"), Inv->ConsumeReservation(HandleA));

	Inv->AddItem(TEXT("fibre"), 5);
	const int32 HandleB = Inv->Reserve(TEXT("fibre"), 5, WorkerB);
	TestEqual(TEXT("release on cancellation frees stock"), Inv->ReleaseAllFor(WorkerB), 1);
	TestEqual(TEXT("freed stock is available"), Inv->CountAvailable(TEXT("fibre")), 5);
	TestFalse(TEXT("released handle is dead"), Inv->ConsumeReservation(HandleB));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnInventoryAcceptAllTest, "Tharrn.Inventory.AcceptAllIsAtomic", TestFlags)
bool FTharrnInventoryAcceptAllTest::RunTest(const FString& Parameters)
{
	UTharrnInventoryComponent* Inv = MakeInventory(2);
	Inv->AddItem(TEXT("stone"), 1);
	TArray<FTharrnItemAmount> Two = {{TEXT("fibre"), 1}, {TEXT("fungus"), 1}};
	TArray<FTharrnItemAmount> One = {{TEXT("fibre"), 3}};
	TestFalse(TEXT("two new kinds do not fit one free slot"), Inv->CanAcceptAll(Two));
	TestTrue(TEXT("one kind fits"), Inv->CanAcceptAll(One));
	TestEqual(TEXT("the check changed nothing"), Inv->TotalItemCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTharrnLayoutIntegrityTest, "Tharrn.District.LayoutIntegrity", TestFlags)
bool FTharrnLayoutIntegrityTest::RunTest(const FString& Parameters)
{
	FTharrnDistrictLayout Layout;
	if (!TestTrue(TEXT("layout loads"), FTharrnData::Get().LoadLayout(TEXT("district_slice.json"), Layout)))
	{
		return false;
	}
	const FTharrnData& Data = FTharrnData::Get();
	TSet<FString> Ids;
	for (const FTharrnDistrictSpawn& Spawn : Layout.Spawns)
	{
		if (Spawn.Type == TEXT("Label"))
		{
			continue;
		}
		if (Ids.Contains(Spawn.Id) || Spawn.Id.IsEmpty())
		{
			AddError(FString::Printf(TEXT("spawn id '%s' missing or repeated"), *Spawn.Id));
		}
		Ids.Add(Spawn.Id);
		const bool bKnown = (Spawn.Type == TEXT("Resource") && Data.FindResource(Spawn.Def))
			|| (Spawn.Type == TEXT("Container") && Data.FindContainer(Spawn.Def))
			|| (Spawn.Type == TEXT("Spawner") && Data.FindCreature(Spawn.Def))
			|| Spawn.Type == TEXT("Water");
		if (!bKnown)
		{
			AddError(FString::Printf(TEXT("spawn '%s' has unknown %s def '%s'"), *Spawn.Id, *Spawn.Type, *Spawn.Def.ToString()));
		}
	}
	const TCHAR* Zones[] = {TEXT("Arrival"), TEXT("Camp"), TEXT("Habitat"), TEXT("Station"), TEXT("Boss")};
	for (const TCHAR* Zone : Zones)
	{
		TestTrue(FString::Printf(TEXT("has a %s room"), Zone), Layout.Rooms.ContainsByPredicate([Zone](const FTharrnDistrictRoom& R) { return R.Name.Contains(Zone); }));
	}
	return true;
}

#endif
