#include "Tests/TharrnSelfTest.h"

#include "Characters/TharrnCreature.h"
#include "Characters/TharrnPlayerCharacter.h"
#include "Combat/TharrnCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/TharrnData.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TharrnHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Persistence/TharrnSaveSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Survival/TharrnVitalsComponent.h"
#include "Tharrn.h"
#include "UnrealClient.h"
#include "World/TharrnContainer.h"
#include "World/TharrnCreatureSpawner.h"
#include "World/TharrnDistrictBuilder.h"
#include "World/TharrnResourceNode.h"
#include "World/TharrnWaterSource.h"

namespace
{
	FString OutputDir()
	{
		return FPaths::ProjectSavedDir() / TEXT("TharrnSelfTest");
	}

	template <typename T>
	T* Spawned(ATharrnDistrictBuilder* Builder, const TCHAR* Id)
	{
		return Builder ? Cast<T>(Builder->FindSpawned(Id)) : nullptr;
	}

	int32 SumCounts(const TMap<FName, int32>& Counts)
	{
		int32 Sum = 0;
		for (const auto& Pair : Counts)
		{
			Sum += Pair.Value;
		}
		return Sum;
	}
}

bool UTharrnSelfTest::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	FString Value;
	return World && World->IsGameWorld() && FParse::Value(FCommandLine::Get(), TEXT("TharrnSelfTest="), Value);
}

TStatId UTharrnSelfTest::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UTharrnSelfTest, STATGROUP_Tickables);
}

void UTharrnSelfTest::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	FParse::Value(FCommandLine::Get(), TEXT("TharrnSelfTest="), Mode);
	if (Mode == TEXT("M1"))
	{
		BuildM1();
	}
	else
	{
		UE_LOG(LogTharrn, Error, TEXT("TharrnSelfTest: unknown mode '%s'"), *Mode);
	}
	bCanShoot = FApp::CanEverRender() && !FParse::Param(FCommandLine::Get(), TEXT("TharrnNoShots"));
	bActive = Steps.Num() > 0;
	StepStart = InWorld.GetTimeSeconds();
	IFileManager::Get().MakeDirectory(*OutputDir(), true);
}

void UTharrnSelfTest::AddStep(const FString& Name, float Timeout, TFunction<bool(float)> Run)
{
	Steps.Add({Name, Timeout, MoveTemp(Run)});
}

bool UTharrnSelfTest::Check(bool bCondition, const FString& Name, const FString& Detail)
{
	const FString StepName = Steps.IsValidIndex(Current) ? Steps[Current].Name : TEXT("end");
	Checks.Add({StepName, Name, bCondition, Detail});
	UE_LOG(LogTharrn, Display, TEXT("TharrnSelfTest [%s] %s: %s %s"), *StepName, bCondition ? TEXT("PASS") : TEXT("FAIL"), *Name, *Detail);
	return bCondition;
}

bool UTharrnSelfTest::Require(bool bCondition, const FString& Name)
{
	return bCondition || Check(false, Name);
}

ATharrnPlayerCharacter* UTharrnSelfTest::Player() const
{
	return Cast<ATharrnPlayerCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

ATharrnDistrictBuilder* UTharrnSelfTest::Builder() const
{
	return ATharrnDistrictBuilder::Find(GetWorld());
}

ATharrnCreature* UTharrnSelfTest::FirstCreature() const
{
	if (ATharrnCreatureSpawner* Spawner = Spawned<ATharrnCreatureSpawner>(Builder(), TEXT("spawn.habitat.skitter.01")))
	{
		for (const TWeakObjectPtr<ATharrnCreature>& Creature : Spawner->GetAlive())
		{
			if (Creature.IsValid() && !Creature->IsDead())
			{
				return Creature.Get();
			}
		}
	}
	return nullptr;
}

FVector UTharrnSelfTest::RoomCenter(const FString& Key) const
{
	if (const ATharrnDistrictBuilder* B = Builder())
	{
		for (const FTharrnDistrictRoom& Room : B->GetLayout().Rooms)
		{
			if (Room.Name.Contains(Key))
			{
				const FVector2D Mid = (Room.Min + Room.Max) * 0.5f;
				return FVector(Mid.X, Mid.Y, Room.FloorZ + 100.f);
			}
		}
	}
	return FVector::ZeroVector;
}

void UTharrnSelfTest::FaceTowards(const FVector& Location, float Pitch)
{
	ATharrnPlayerCharacter* P = Player();
	if (!P)
	{
		return;
	}
	const float Yaw = (Location - P->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
	P->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
	{
		PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
	}
}

void UTharrnSelfTest::PlaceNear(AActor* Goal, float Distance, float ApproachYaw)
{
	ATharrnPlayerCharacter* P = Player();
	if (!P || !Goal)
	{
		return;
	}
	FVector Origin;
	FVector Extent;
	Goal->GetActorBounds(true, Origin, Extent);
	const FVector Dir = FRotator(0.f, ApproachYaw, 0.f).Vector();
	const float Offset = FMath::Max(Extent.X, Extent.Y) + Distance;
	FVector Location = Origin - Dir * Offset;
	Location.Z = Origin.Z - Extent.Z + P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f;
	P->TeleportTo(Location, FRotator(0.f, ApproachYaw, 0.f), false, true);
	P->GetCharacterMovement()->StopMovementImmediately();
	FaceTowards(Origin);
}

void UTharrnSelfTest::SetAllBrains(bool bEnabled)
{
	for (TActorIterator<ATharrnCreature> It(GetWorld()); It; ++It)
	{
		It->SetBrainEnabled(bEnabled);
	}
}

int32 UTharrnSelfTest::SlotOf(FName ItemId) const
{
	const ATharrnPlayerCharacter* P = Player();
	if (P)
	{
		const TArray<FTharrnItemStack>& Slots = P->Inventory->GetSlots();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (!Slots[Index].IsEmpty() && Slots[Index].ItemId == ItemId)
			{
				return Index;
			}
		}
	}
	return INDEX_NONE;
}

TMap<FName, int32> UTharrnSelfTest::CountsOf(const UTharrnInventoryComponent* Inventory) const
{
	TMap<FName, int32> Counts;
	if (Inventory)
	{
		for (const FTharrnItemStack& Stack : Inventory->GetSlots())
		{
			if (!Stack.IsEmpty())
			{
				Counts.FindOrAdd(Stack.ItemId) += Stack.Count;
			}
		}
	}
	return Counts;
}

FString UTharrnSelfTest::Describe(const TMap<FName, int32>& Counts) const
{
	TArray<FString> Parts;
	for (const auto& Pair : Counts)
	{
		Parts.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key.ToString(), Pair.Value));
	}
	Parts.Sort();
	return FString::Join(Parts, TEXT(" "));
}

bool UTharrnSelfTest::Shot(const FString& Name, const FString& Caption, float Elapsed, float& ShotTime)
{
	ATharrnHUD* HUD = ATharrnHUD::Get(Player());
	if (HUD)
	{
		HUD->Caption = Caption;
	}
	if (!bCanShoot)
	{
		return true;
	}
	if (Elapsed < ShotTime + 0.7f)
	{
		return false;
	}
	if (!bShotPending)
	{
		FScreenshotRequest::RequestScreenshot(OutputDir() / Name + TEXT(".png"), true, false);
		bShotPending = true;
		ShotRequestedAt = Elapsed;
		return false;
	}
	if (Elapsed < ShotRequestedAt + 0.5f)
	{
		return false;
	}
	bShotPending = false;
	if (HUD)
	{
		HUD->Caption.Reset();
	}
	Notes.Add(TEXT("screenshot ") + Name);
	return true;
}

void UTharrnSelfTest::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!bActive || !World)
	{
		return;
	}
	if (!Steps.IsValidIndex(Current))
	{
		Finish();
		return;
	}
	const float Now = World->GetTimeSeconds();
	const float Elapsed = Now - StepStart;
	bool bDone = Steps[Current].Run(Elapsed);
	if (!bDone && Elapsed > Steps[Current].Timeout)
	{
		Check(false, TEXT("step finished in time"), FString::Printf(TEXT("timed out in phase %d"), Phase));
		bDone = true;
	}
	if (bDone)
	{
		UE_LOG(LogTharrn, Display, TEXT("TharrnSelfTest step '%s' done in %.1fs"), *Steps[Current].Name, Elapsed);
		++Current;
		Phase = 0;
		Mark = 0.f;
		Counter = 0;
		bShotPending = false;
		StepStart = Now;
	}
}

void UTharrnSelfTest::Finish()
{
	bActive = false;
	int32 Passed = 0;
	TArray<TSharedPtr<FJsonValue>> CheckValues;
	for (const FCheck& C : Checks)
	{
		Passed += C.bPass ? 1 : 0;
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("step"), C.Step);
		Object->SetStringField(TEXT("check"), C.Name);
		Object->SetBoolField(TEXT("pass"), C.bPass);
		Object->SetStringField(TEXT("detail"), C.Detail);
		CheckValues.Add(MakeShared<FJsonValueObject>(Object));
	}
	TArray<TSharedPtr<FJsonValue>> NoteValues;
	for (const FString& Note : Notes)
	{
		NoteValues.Add(MakeShared<FJsonValueString>(Note));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), Mode);
	Root->SetStringField(TEXT("finished_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("map"), GetWorld()->GetMapName());
	Root->SetNumberField(TEXT("passed"), Passed);
	Root->SetNumberField(TEXT("failed"), Checks.Num() - Passed);
	Root->SetBoolField(TEXT("screenshots"), bCanShoot);
	Root->SetArrayField(TEXT("checks"), CheckValues);
	Root->SetArrayField(TEXT("notes"), NoteValues);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(Json, *(OutputDir() / Mode + TEXT(".json")));
	UE_LOG(LogTharrn, Display, TEXT("TharrnSelfTest %s finished: %d passed, %d failed"), *Mode, Passed, Checks.Num() - Passed);
	if (!FParse::Param(FCommandLine::Get(), TEXT("TharrnSelfTestStay")))
	{
		FPlatformMisc::RequestExit(false, TEXT("TharrnSelfTest"));
	}
}

void UTharrnSelfTest::BuildM1()
{

	AddStep(TEXT("ready"), 40.f, [this](float T)
	{
		ATharrnDistrictBuilder* B = Builder();
		ATharrnPlayerCharacter* P = Player();
		if (!B || !B->HasSpawnedGameplay() || !P || UTharrnSaveSubsystem::ProfileIdOf(P).IsEmpty() || T < 3.f)
		{
			return false;
		}
		Check(FTharrnData::Get().GetErrors().Num() == 0, TEXT("gameplay data loads without errors"), FString::Printf(TEXT("%d errors"), FTharrnData::Get().GetErrors().Num()));
		Check(B->GetBlockCount() > 60, TEXT("greybox district built"), FString::Printf(TEXT("%d blocks"), B->GetBlockCount()));
		Check(FVector::Dist(P->GetActorLocation(), B->GetArrivalPoint()) < 400.f, TEXT("player arrives at the arrival platform"));
		SetAllBrains(false);
		P->Vitals->Recover(1.f, 80.f, 80.f);
		return true;
	});

	AddStep(TEXT("shot_arrival"), 8.f, [this](float T)
	{
		if (Phase == 0)
		{
			Phase = 1;
			Mark = T;
			if (AActor* Crate = Builder()->FindSpawned(TEXT("cont.arrival.crate")))
			{
				PlaceNear(Crate, 520.f, 20.f);
			}
		}
		return Shot(TEXT("01_arrival"), TEXT("M1 - Arrival chamber: platform, dark arrival gate, expedition supply crate"), T, Mark);
	});

	AddStep(TEXT("supply_crate"), 10.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnContainer* Crate = Spawned<ATharrnContainer>(Builder(), TEXT("cont.arrival.crate"));
		if (!Require(P && Crate, TEXT("supply crate exists")))
		{
			return true;
		}
		if (Phase == 0)
		{
			PlaceNear(Crate, 90.f, 20.f);
			FString Reason;
			Check(P->BeginInteract(Crate, Reason) && P->OpenContainer == Crate, TEXT("crate opens"), Reason);
			Snapshot = CountsOf(Crate->Inventory);
			Number = SumCounts(Snapshot) + P->Inventory->TotalItemCount();
			Phase = 1;
			Mark = T;
		}
		if (Phase == 1)
		{
			if (!Shot(TEXT("02_crate_open"), TEXT("M1 - Container panel: click a row to take it (server-validated transfer)"), T, Mark))
			{
				return false;
			}
			for (int32 Slot = 0; Slot < Crate->Inventory->MaxSlots; ++Slot)
			{
				if (!Crate->Inventory->GetSlots()[Slot].IsEmpty())
				{
					P->TransferBetween(Crate->Inventory, Slot, 0, P->Inventory);
				}
			}
			Check(P->Inventory->CountItem(TEXT("pick")) == 1 && P->Inventory->CountItem(TEXT("cleaver")) == 1, TEXT("tool and weapon recovered from the crate"), Describe(CountsOf(P->Inventory)));
			Check(Crate->Inventory->IsEmpty(), TEXT("crate emptied"));
			Check(P->Inventory->TotalItemCount() + Crate->Inventory->TotalItemCount() == static_cast<int32>(Number), TEXT("item quantities conserved"),
				FString::Printf(TEXT("before %d after %d"), static_cast<int32>(Number), P->Inventory->TotalItemCount() + Crate->Inventory->TotalItemCount()));
			Check(Crate->bInitialContentsGranted, TEXT("starting contents marked as granted"));
			FString Reason;
			Check(!P->TransferBetween(P->Inventory, 0, 0, Spawned<ATharrnContainer>(Builder(), TEXT("cont.camp.storage"))->Inventory), TEXT("transfer to a container that is not open is refused"));
			P->CloseContainer();
			return true;
		}
		return false;
	});

	AddStep(TEXT("gather_by_hand"), 20.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnResourceNode* Node = Spawned<ATharrnResourceNode>(Builder(), TEXT("res.camp.fibre.01"));
		if (!Require(P && Node && Node->GetDef(), TEXT("fibre plant exists")))
		{
			return true;
		}
		const FTharrnResourceDef* Def = Node->GetDef();
		if (Phase == 0)
		{
			PlaceNear(Node, 70.f, 0.f);
			Number = P->Inventory->CountItem(TEXT("fibre"));
			Phase = 1;
			Mark = T;
		}
		if (Phase == 1)
		{
			if (!P->InteractTarget && !Node->IsDepleted())
			{
				FString Reason;
				if (!P->BeginInteract(Node, Reason))
				{
					Check(false, TEXT("hand gathering starts"), Reason);
					return true;
				}
			}
			if (Counter == 0 && P->InteractTarget)
			{
				// Mid-gather, with the progress bar showing.
				if (Shot(TEXT("03_gathering"), TEXT("M1 - Timed gathering by hand: cave fibre (progress bar under the crosshair)"), T, Mark))
				{
					Counter = 1;
				}
				return false;
			}
			if (Node->IsDepleted() && !P->InteractTarget)
			{
				const int32 PerGather = Def->Yields.Num() > 0 ? Def->Yields[0].Count : 0;
				const int32 Gained = P->Inventory->CountItem(TEXT("fibre")) - static_cast<int32>(Number);
				Check(Gained == PerGather * Def->Charges, TEXT("each gather yields exactly the defined amount"), FString::Printf(TEXT("gained %d, expected %d"), Gained, PerGather * Def->Charges));
				Check(!Node->Mesh->IsVisible(), TEXT("depleted node is cleared from the world"));
				FString Reason;
				Check(!P->BeginInteract(Node, Reason) && Reason.Contains(TEXT("Depleted")), TEXT("depleted node refuses gathering"), Reason);
				return true;
			}
		}
		return false;
	});

	AddStep(TEXT("tool_gathering"), 25.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnResourceNode* Node = Spawned<ATharrnResourceNode>(Builder(), TEXT("res.habitat.stone.01"));
		if (!Require(P && Node && Node->GetDef(), TEXT("stone outcrop exists")))
		{
			return true;
		}
		const FTharrnResourceDef* Def = Node->GetDef();
		const int32 PickSlot = SlotOf(TEXT("pick"));
		if (Phase == 0)
		{
			PlaceNear(Node, 70.f, 90.f);
			int32 EmptyHand = 0;
			while (EmptyHand < ATharrnPlayerCharacter::HotbarSize && !P->Inventory->GetSlots()[EmptyHand].IsEmpty())
			{
				++EmptyHand;
			}
			P->ActiveSlot = EmptyHand;
			FString Reason;
			Check(!P->BeginInteract(Node, Reason) && Reason.Contains(TEXT("tool")), TEXT("mining without a pick is refused with a reason"), Reason);
			Check(PickSlot != INDEX_NONE && PickSlot < ATharrnPlayerCharacter::HotbarSize, TEXT("pick is on the hotbar"));
			FString Message;
			P->UseSlot(PickSlot, Message);
			Check(P->ActiveSlot == PickSlot && P->GetActiveToolTag() == TEXT("mining"), TEXT("pick held"), Message);
			Number = P->Inventory->CountItem(TEXT("stone"));
			Number2 = P->Inventory->GetSlots()[PickSlot].Durability;
			Phase = 1;
		}
		if (Phase == 1)
		{
			if (!P->InteractTarget && !Node->IsDepleted())
			{
				FString Reason;
				if (!P->BeginInteract(Node, Reason))
				{
					Check(false, TEXT("mining starts"), Reason);
					return true;
				}
			}
			if (Node->IsDepleted() && !P->InteractTarget)
			{
				int32 Expected = 0;
				for (const FTharrnItemAmount& Yield : Def->Yields)
				{
					Expected += Yield.ItemId == TEXT("stone") ? Yield.Count * Def->Charges : 0;
				}
				const int32 Gained = P->Inventory->CountItem(TEXT("stone")) - static_cast<int32>(Number);
				Check(Gained == Expected, TEXT("mining yields the defined stone"), FString::Printf(TEXT("gained %d, expected %d"), Gained, Expected));
				const float Wear = Number2 - P->Inventory->GetSlots()[PickSlot].Durability;
				Check(FMath::IsNearlyEqual(Wear, Def->ToolWear * Def->Charges), TEXT("the pick wears per use"), FString::Printf(TEXT("wear %.1f"), Wear));
				return true;
			}
		}
		return false;
	});

	AddStep(TEXT("eat_and_drink"), 15.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnWaterSource* Water = Spawned<ATharrnWaterSource>(Builder(), TEXT("water.camp.seep"));
		if (!Require(P && Water, TEXT("water seep exists")))
		{
			return true;
		}
		if (Phase == 0)
		{
			const int32 Slot = SlotOf(TEXT("provisions"));
			const FTharrnItemDef* Food = FTharrnData::Get().FindItem(TEXT("provisions"));
			P->Vitals->Food = 40.f;
			const int32 Before = P->Inventory->CountItem(TEXT("provisions"));
			FString Message;
			Check(Slot != INDEX_NONE && P->UseSlot(Slot, Message), TEXT("provisions can be eaten"), Message);
			Check(FMath::IsNearlyEqual(P->Vitals->Food, FMath::Min(100.f, 40.f + Food->Food), 0.01f), TEXT("eating restores the defined food"), FString::Printf(TEXT("food %.1f"), P->Vitals->Food));
			Check(P->Vitals->HasEffect(Food->Effect), TEXT("cooked provisions grant their effect"), Food->Effect.ToString());
			Check(P->Inventory->CountItem(TEXT("provisions")) == Before - 1, TEXT("one portion consumed"));
			PlaceNear(Water, 60.f, -60.f);
			P->Vitals->Water = 30.f;
			FString Reason;
			Check(P->BeginInteract(Water, Reason), TEXT("drinking starts"), Reason);
			Phase = 1;
		}
		if (Phase == 1 && !P->InteractTarget)
		{
			const float Expected = FMath::Min(P->Vitals->MaxWater, 30.f + FTharrnData::Get().GetSurvival().WaterPerDrink);
			Check(FMath::Abs(P->Vitals->Water - Expected) < 0.5f, TEXT("drinking restores the defined water"), FString::Printf(TEXT("water %.1f"), P->Vitals->Water));
			return true;
		}
		return false;
	});

	AddStep(TEXT("camp_storage"), 10.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnContainer* Storage = Spawned<ATharrnContainer>(Builder(), TEXT("cont.camp.storage"));
		if (!Require(P && Storage, TEXT("camp storage exists")))
		{
			return true;
		}
		if (Phase == 0)
		{
			PlaceNear(Storage, 90.f, 0.f);
			FString Reason;
			Check(P->BeginInteract(Storage, Reason), TEXT("storage opens"), Reason);
			Number = P->Inventory->CountItem(TEXT("stone")) + Storage->Inventory->CountItem(TEXT("stone"));
			const int32 Slot = SlotOf(TEXT("stone"));
			Check(P->TransferBetween(P->Inventory, Slot, 10, Storage->Inventory), TEXT("store 10 stone"));
			Check(Storage->Inventory->CountItem(TEXT("stone")) == 10, TEXT("storage holds 10 stone"));
			int32 StorageSlot = 0;
			while (StorageSlot < Storage->Inventory->MaxSlots && Storage->Inventory->GetSlots()[StorageSlot].ItemId != TEXT("stone"))
			{
				++StorageSlot;
			}
			Check(P->TransferBetween(Storage->Inventory, StorageSlot, 4, P->Inventory), TEXT("take 4 back"));
			Check(Storage->Inventory->CountItem(TEXT("stone")) == 6, TEXT("storage keeps 6"));
			Check(P->Inventory->CountItem(TEXT("stone")) + Storage->Inventory->CountItem(TEXT("stone")) == static_cast<int32>(Number), TEXT("stone conserved across storage"));
			Phase = 1;
			Mark = T;
		}
		if (Phase == 1 && Shot(TEXT("04_storage"), TEXT("M1 - Pack and camp storage side by side; transfers validated on the server"), T, Mark))
		{
			P->CloseContainer();
			return true;
		}
		return false;
	});

	AddStep(TEXT("creature_telegraph"), 25.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnCreature* Creature = FirstCreature();
		if (!Require(P && Creature, TEXT("hostile creature present")))
		{
			return true;
		}
		if (Phase == 0)
		{
			FString Message;
			P->UseSlot(SlotOf(TEXT("cleaver")), Message);
			P->Vitals->Recover(1.f, 80.f, 80.f);
			PlaceNear(Creature, 420.f, 0.f);
			Creature->SetBrainEnabled(true);
			Number = P->Vitals->Health;
			Phase = 1;
		}
		if (Phase == 1 && Creature->Combat->Phase == ETharrnAttackPhase::Windup)
		{
			Mark = T;
			Number = P->Vitals->Health;
			Phase = 2;
			Check(Creature->GetTarget() == P, TEXT("creature acquires the player"));
		}
		if (Phase == 2)
		{
			if (Counter == 0)
			{
				// Shot while the windup disc is showing, from the side so the player's body does not hide it.
				if (bCanShoot && !bShotPending)
				{
					if (APlayerController* PC = Cast<APlayerController>(P->GetController()))
					{
						const float Yaw = (Creature->GetActorLocation() - P->GetActorLocation()).GetSafeNormal2D().Rotation().Yaw;
						PC->SetControlRotation(FRotator(-22.f, Yaw + 55.f, 0.f));
					}
					FScreenshotRequest::RequestScreenshot(OutputDir() / TEXT("05_telegraph.png"), true, false);
					bShotPending = true;
					Notes.Add(TEXT("screenshot 05_telegraph"));
				}
				Counter = 1;
			}
			if (Creature->Combat->Phase == ETharrnAttackPhase::Active)
			{
				Number2 = T - Mark;
				Phase = 3;
			}
		}
		if (Phase == 3 && Creature->Combat->Phase == ETharrnAttackPhase::Recovery)
		{
			Creature->SetBrainEnabled(false);
			const float Expected = Creature->Def.Attack.Windup;
			Check(Number2 >= Expected - 0.1f && Number2 >= 0.5f, TEXT("attack is telegraphed long enough to read"), FString::Printf(TEXT("windup %.2fs"), Number2));
			const float Taken = Number - P->Vitals->Health;
			Check(FMath::IsNearlyEqual(Taken, Creature->Def.Attack.Damage, 0.01f), TEXT("telegraphed attack deals its defined damage"), FString::Printf(TEXT("took %.1f"), Taken));
			Check(Creature->HasNavigatedPath() || !Creature->UsedDirectChase(), TEXT("creature pathed on the navmesh"),
				Creature->HasNavigatedPath() ? TEXT("navmesh path") : TEXT("no path request made"));
			bShotPending = false;
			return true;
		}
		return false;
	});

	AddStep(TEXT("block_parry_dodge"), 10.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnCreature* Creature = FirstCreature();
		if (!Require(P && Creature, TEXT("combatants present")))
		{
			return true;
		}
		const FTharrnMeleeStats Bite = Creature->Def.Attack;
		auto Arrange = [this, P, Creature]()
		{
			const FVector Front = P->GetActorLocation() + P->GetActorForwardVector() * 150.f;
			Creature->TeleportTo(Front, (P->GetActorLocation() - Front).Rotation(), false, true);
			Creature->GetCharacterMovement()->StopMovementImmediately();
		};
		if (Phase == 0)
		{
			P->Vitals->Recover(1.f, 80.f, 80.f);
			FaceTowards(Creature->GetActorLocation());
			Arrange();
			P->Combat->SetBlocking(true);
			Mark = T;
			Phase = 1;
		}
		if (Phase == 1 && T > Mark + 0.45f)
		{
			// Held block, past the parry window.
			const float Health = P->Vitals->Health;
			const float Stamina = P->Vitals->Stamina;
			float Damage = 0.f;
			const ETharrnHitOutcome Outcome = UTharrnCombatComponent::ResolveHit(Creature, P, Bite, Damage);
			Check(Outcome == ETharrnHitOutcome::Blocked, TEXT("a held block blocks"), UEnum::GetValueAsString(Outcome));
			Check(FMath::IsNearlyEqual(Health - P->Vitals->Health, Bite.Damage * (1.f - P->Combat->BlockDamageReduction), 0.01f), TEXT("block reduces damage by the defined share"),
				FString::Printf(TEXT("took %.2f"), Health - P->Vitals->Health));
			Check(FMath::IsNearlyEqual(Stamina - P->Vitals->Stamina, Bite.Damage * P->Combat->BlockStaminaPerDamage, 0.01f), TEXT("blocking costs stamina per hit"),
				FString::Printf(TEXT("stamina -%.2f"), Stamina - P->Vitals->Stamina));

			// Fresh block: inside the parry window.
			P->Combat->SetBlocking(false);
			P->Combat->SetBlocking(true);
			const float Before = P->Vitals->Health;
			const ETharrnHitOutcome Parry = UTharrnCombatComponent::ResolveHit(Creature, P, Bite, Damage);
			Check(Parry == ETharrnHitOutcome::Parried && FMath::IsNearlyEqual(Before, P->Vitals->Health), TEXT("a just-in-time block parries for no damage"), UEnum::GetValueAsString(Parry));
			Check(Creature->Combat->bStaggered, TEXT("a parry staggers the attacker"));
			P->Combat->SetBlocking(false);
			Mark = T;
			Phase = 2;
		}
		if (Phase == 2 && T > Mark + 1.2f)
		{
			Arrange();
			P->Vitals->Recover(1.f, 80.f, 80.f);
			Check(P->Combat->TryDodge(-P->GetActorForwardVector()), TEXT("dodge spends stamina and starts"));
			float Damage = 0.f;
			const float Before = P->Vitals->Health;
			const ETharrnHitOutcome Dodge = UTharrnCombatComponent::ResolveHit(Creature, P, Bite, Damage);
			Check(Dodge == ETharrnHitOutcome::Dodged && FMath::IsNearlyEqual(Before, P->Vitals->Health), TEXT("a dodge avoids the hit"), UEnum::GetValueAsString(Dodge));
			Mark = T;
			Phase = 3;
		}
		if (Phase == 3 && T > Mark + 0.8f)
		{
			FaceTowards(Creature->GetActorLocation());
			Arrange();
			float Damage = 0.f;
			const float Before = P->Vitals->Health;
			const ETharrnHitOutcome Hit = UTharrnCombatComponent::ResolveHit(Creature, P, Bite, Damage);
			Check((Hit == ETharrnHitOutcome::Hit || Hit == ETharrnHitOutcome::Staggered) && FMath::IsNearlyEqual(Before - P->Vitals->Health, Bite.Damage), TEXT("an unguarded hit deals full damage"),
				FString::Printf(TEXT("%s, took %.1f"), *UEnum::GetValueAsString(Hit), Before - P->Vitals->Health));
			P->Vitals->Recover(1.f, 80.f, 80.f);
			return true;
		}
		return false;
	});

	AddStep(TEXT("player_kills_creature"), 30.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		ATharrnCreature* Creature = Cast<ATharrnCreature>(Target.Get());
		if (Phase == 0)
		{
			Creature = FirstCreature();
			if (!Require(P && Creature, TEXT("creature to fight")))
			{
				return true;
			}
			Target = Creature;
			P->Vitals->Recover(1.f, 80.f, 80.f);
			Number = Creature->Vitals->Health;
			Number2 = P->Inventory->GetSlots()[P->ActiveSlot].Durability;
			Counter = 0;
			Phase = 1;
		}
		if (!P || !Creature)
		{
			Check(false, TEXT("creature still tracked"));
			return true;
		}
		const FTharrnMeleeStats Cleaver = FTharrnData::Get().FindItem(TEXT("cleaver"))->Melee;
		if (Phase == 1 && P->Combat->Phase == ETharrnAttackPhase::None && !P->Combat->bStaggered)
		{
			if (Creature->IsDead())
			{
				Phase = 2;
				Mark = T;
			}
			else
			{
				// Keep the target squarely in front (stagger knockback pushes it away between swings).
				const FVector Front = P->GetActorLocation() + P->GetActorForwardVector() * 130.f;
				Creature->TeleportTo(Front, (P->GetActorLocation() - Front).Rotation(), false, true);
				Creature->GetCharacterMovement()->StopMovementImmediately();
				FaceTowards(Creature->GetActorLocation());
				const float Before = Creature->Vitals->Health;
				if (Counter > 0)
				{
					const float Dealt = Number - Before;
					Check(FMath::IsNearlyEqual(Dealt, FMath::Min(Number, Cleaver.Damage), 0.01f), FString::Printf(TEXT("swing %d deals the cleaver's damage"), Counter), FString::Printf(TEXT("%.1f"), Dealt));
				}
				Number = Before;
				P->Vitals->Stamina = P->Vitals->MaxStamina;
				if (P->Attack())
				{
					++Counter;
				}
			}
		}
		if (Phase == 2 && T > Mark + 0.3f)
		{
			const int32 ExpectedSwings = FMath::CeilToInt(Creature->Def.MaxHealth / Cleaver.Damage);
			Check(Counter == ExpectedSwings, TEXT("creature dies after the expected number of hits"), FString::Printf(TEXT("%d swings, expected %d"), Counter, ExpectedSwings));
			const float Wear = Number2 - P->Inventory->GetSlots()[P->ActiveSlot].Durability;
			Check(FMath::IsNearlyEqual(Wear, static_cast<float>(Counter)), TEXT("the cleaver wears per landed hit"), FString::Printf(TEXT("wear %.0f"), Wear));
			ATharrnContainer* Remains = nullptr;
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				if (It->ContainerDefId == TEXT("creature_remains"))
				{
					Remains = *It;
				}
			}
			if (Check(Remains != nullptr, TEXT("remains left with the drops")))
			{
				Check(Remains->Inventory->CountItem(TEXT("raw_meat")) == 2, TEXT("drops match the creature definition"), Describe(CountsOf(Remains->Inventory)));
				PlaceNear(Remains, 60.f, 0.f);
				FString Reason;
				P->BeginInteract(Remains, Reason);
				const int32 Meat = P->Inventory->CountItem(TEXT("raw_meat"));
				UTharrnInventoryComponent::TransferAll(Remains->Inventory, P->Inventory);
				Check(P->Inventory->CountItem(TEXT("raw_meat")) == Meat + 2, TEXT("drops looted into the pack"));
				Target2 = Remains;
			}
			Phase = 3;
			Mark = T;
		}
		if (Phase == 3 && T > Mark + 0.5f)
		{
			Check(!Target2.IsValid(), TEXT("emptied remains disappear"));
			P->CloseContainer();
			return true;
		}
		return false;
	});

	AddStep(TEXT("injury_and_recovery"), 15.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		if (!P)
		{
			return true;
		}
		if (Phase == 0)
		{
			Snapshot = CountsOf(P->Inventory);
			P->Vitals->ApplyDamage(10000.f, nullptr);
			Check(P->IsDead(), TEXT("lethal damage incapacitates"));
			Check(P->Inventory->IsEmpty(), TEXT("pack is left behind"));
			ATharrnContainer* Cache = nullptr;
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				if (It->ContainerDefId == TEXT("recovery_cache"))
				{
					Cache = *It;
				}
			}
			if (Check(Cache != nullptr, TEXT("a recovery cache marks where they fell")))
			{
				Check(Describe(CountsOf(Cache->Inventory)) == Describe(Snapshot), TEXT("the cache holds exactly the pack"), Describe(CountsOf(Cache->Inventory)));
				Target = Cache;
			}
			Phase = 1;
		}
		if (Phase == 1 && !P->IsDead() && !P->IsRecovering())
		{
			const float Distance = FVector::Dist2D(P->GetActorLocation(), Builder()->GetRecoveryPoint());
			Check(Distance < 300.f, TEXT("recovery happens at camp"), FString::Printf(TEXT("%.0f from the recovery point"), Distance));
			Check(FMath::IsNearlyEqual(P->Vitals->Health, P->Vitals->MaxHealth * FTharrnData::Get().GetSurvival().RecoveryHealthFraction, 0.5f), TEXT("recovered at the defined health"));
			if (ATharrnContainer* Cache = Cast<ATharrnContainer>(Target.Get()))
			{
				PlaceNear(Cache, 60.f, 0.f);
				FString Reason;
				P->BeginInteract(Cache, Reason);
				UTharrnInventoryComponent::TransferAll(Cache->Inventory, P->Inventory);
				Check(Describe(CountsOf(P->Inventory)) == Describe(Snapshot), TEXT("everything recovered from the cache"), Describe(CountsOf(P->Inventory)));
				P->CloseContainer();
			}
			return true;
		}
		return false;
	});

	AddStep(TEXT("save_and_load"), 15.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		UTharrnSaveSubsystem* Saves = GetWorld()->GetSubsystem<UTharrnSaveSubsystem>();
		ATharrnContainer* Storage = Spawned<ATharrnContainer>(Builder(), TEXT("cont.camp.storage"));
		ATharrnContainer* Crate = Spawned<ATharrnContainer>(Builder(), TEXT("cont.arrival.crate"));
		ATharrnResourceNode* Fibre = Spawned<ATharrnResourceNode>(Builder(), TEXT("res.camp.fibre.01"));
		if (!Require(P && Saves && Storage && Crate && Fibre, TEXT("save fixtures present")))
		{
			return true;
		}
		const FString Slot = TEXT("TharrnSelfTest");
		if (Phase == 0)
		{
			// A runtime cache that must come back after the load.
			const int32 FibreSlot = SlotOf(TEXT("fibre"));
			Check(P->DropSlot(FibreSlot), TEXT("drop a stack as a cache"));
			P->Vitals->Health = 77.f;
			P->Vitals->Food = 66.f;
			P->Vitals->Water = 55.f;
			Snapshot = CountsOf(P->Inventory);
			Snapshot2 = CountsOf(Storage->Inventory);
			int32 Dropped = 0;
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				Dropped += It->ContainerDefId == TEXT("dropped_pack") ? It->Inventory->TotalItemCount() : 0;
			}
			Number = Dropped;
			Number2 = P->ActiveSlot;
			Check(Saves->SaveToSlot(Slot), TEXT("save succeeds"), Saves->GetLastError());

			// Disturb everything the save should restore.
			P->Inventory->ResetContents();
			P->Inventory->AddItem(TEXT("stone"), 3);
			P->Vitals->Health = 10.f;
			P->ActiveSlot = 7;
			Storage->Inventory->ResetContents();
			Fibre->Regrow();
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				if (It->ContainerDefId == TEXT("dropped_pack"))
				{
					It->Destroy();
				}
			}
			ATharrnContainer* Stray = ATharrnContainer::SpawnCache(GetWorld(), TEXT("dropped_pack"), P->GetActorLocation() + FVector(200.f, 0.f, 0.f));
			Stray->Inventory->AddItem(TEXT("scrap_metal"), 5);
			P->TeleportTo(P->GetActorLocation() + FVector(500.f, 0.f, 0.f), P->GetActorRotation(), false, true);
			Mark = T;
			Phase = 1;
		}
		if (Phase == 1 && T > Mark + 0.3f)
		{
			Check(Saves->LoadFromSlot(Slot), TEXT("load succeeds"), Saves->GetLastError());
			Phase = 2;
			Mark = T;
		}
		if (Phase == 2 && T > Mark + 0.3f)
		{
			Check(Describe(CountsOf(P->Inventory)) == Describe(Snapshot), TEXT("player pack restored exactly"), Describe(CountsOf(P->Inventory)));
			Check(FMath::IsNearlyEqual(P->Vitals->Health, 77.f) && FMath::IsNearlyEqual(P->Vitals->Food, 66.f, 0.2f) && FMath::IsNearlyEqual(P->Vitals->Water, 55.f, 0.2f),
				TEXT("vitals restored"), FString::Printf(TEXT("H %.1f F %.1f W %.1f"), P->Vitals->Health, P->Vitals->Food, P->Vitals->Water));
			Check(P->ActiveSlot == static_cast<int32>(Number2), TEXT("held item restored"));
			Check(Describe(CountsOf(Storage->Inventory)) == Describe(Snapshot2), TEXT("camp storage restored"), Describe(CountsOf(Storage->Inventory)));
			Check(Fibre->IsDepleted(), TEXT("depleted node stays depleted"));
			Check(Crate->Inventory->IsEmpty() && Crate->bInitialContentsGranted, TEXT("starting supplies are not granted twice"));
			int32 Dropped = 0;
			int32 Strays = 0;
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				if (It->ContainerDefId == TEXT("dropped_pack") && !It->IsActorBeingDestroyed())
				{
					Dropped += It->Inventory->TotalItemCount();
					Strays += It->Inventory->CountItem(TEXT("scrap_metal"));
				}
			}
			Check(Dropped == static_cast<int32>(Number), TEXT("dropped cache recreated with its contents"), FString::Printf(TEXT("%d items"), Dropped));
			Check(Strays == 0, TEXT("caches created after the save are rolled back"));

			// Loading twice must not duplicate anything.
			Saves->LoadFromSlot(Slot);
			Phase = 3;
			Mark = T;
		}
		if (Phase == 3 && T > Mark + 0.3f)
		{
			int32 Dropped = 0;
			for (TActorIterator<ATharrnContainer> It(GetWorld()); It; ++It)
			{
				Dropped += (It->ContainerDefId == TEXT("dropped_pack") && !It->IsActorBeingDestroyed()) ? It->Inventory->TotalItemCount() : 0;
			}
			Check(Dropped == static_cast<int32>(Number) && Describe(CountsOf(P->Inventory)) == Describe(Snapshot), TEXT("a second load duplicates nothing"));
			return true;
		}
		return false;
	});

	AddStep(TEXT("survival_rates"), 15.f, [this](float T)
	{
		ATharrnPlayerCharacter* P = Player();
		if (!P)
		{
			return true;
		}
		const FTharrnSurvivalTuning& Tuning = FTharrnData::Get().GetSurvival();
		if (Phase == 0)
		{
			P->Vitals->Effects.Reset();
			P->Vitals->Food = 80.f;
			P->Vitals->Water = 80.f;
			Number = P->Vitals->Food;
			Number2 = P->Vitals->Water;
			Mark = T;
			Phase = 1;
		}
		if (Phase == 1 && T > Mark + 4.2f)
		{
			const float Elapsed = T - Mark;
			const float Food = Number - P->Vitals->Food;
			const float Water = Number2 - P->Vitals->Water;
			Check(FMath::Abs(Food - Tuning.FoodDrainPerSecond * Elapsed) <= Tuning.FoodDrainPerSecond * 1.5f, TEXT("food drains at the defined rate"), FString::Printf(TEXT("-%.3f in %.1fs"), Food, Elapsed));
			Check(FMath::Abs(Water - Tuning.WaterDrainPerSecond * Elapsed) <= Tuning.WaterDrainPerSecond * 1.5f, TEXT("water drains at the defined rate"), FString::Printf(TEXT("-%.3f in %.1fs"), Water, Elapsed));
			P->Vitals->Food = 0.f;
			P->Vitals->Water = 60.f;
			P->Vitals->Health = 50.f;
			Mark = T;
			Phase = 2;
		}
		if (Phase == 2 && T > Mark + 3.2f)
		{
			const float Lost = 50.f - P->Vitals->Health;
			Check(Lost > Tuning.StarvingHealthLossPerSecond * 1.5f && Lost < Tuning.StarvingHealthLossPerSecond * 4.5f, TEXT("starvation costs health"), FString::Printf(TEXT("-%.2f"), Lost));
			P->Vitals->Recover(1.f, 80.f, 80.f);
			return true;
		}
		return false;
	});

	struct FView
	{
		const TCHAR* Room;
		const TCHAR* Name;
		const TCHAR* Caption;
		FVector Offset;
	};
	static const FView Views[] = {
		{TEXT("Camp"), TEXT("06_camp"), TEXT("M1 - Camp cavern: storage, water seep, fibre and fungus (buildable pad for M2)"), FVector(-1500.f, -900.f, 0.f)},
		{TEXT("Habitat"), TEXT("07_habitat"), TEXT("M1 - Wildlife habitat: stone outcrops, salvage, hostile skitters"), FVector(-1800.f, 900.f, 0.f)},
		{TEXT("Station"), TEXT("08_station"), TEXT("Ruined station shell (clue and repair work arrive in M4)"), FVector(-1400.f, 1200.f, 0.f)},
		{TEXT("Boss"), TEXT("09_boss_gate"), TEXT("Boss enclosure, sealed until M4"), FVector(-2600.f, -600.f, 0.f)},
	};
	for (const FView& View : Views)
	{
		AddStep(FString(TEXT("view_")) + View.Room, 8.f, [this, View](float T)
		{
			ATharrnPlayerCharacter* P = Player();
			if (!P)
			{
				return true;
			}
			if (Phase == 0)
			{
				const FVector Center = RoomCenter(View.Room);
				P->TeleportTo(Center + View.Offset, FRotator::ZeroRotator, false, true);
				FaceTowards(Center, -14.f);
				Mark = T;
				Phase = 1;
			}
			return Shot(View.Name, View.Caption, T, Mark);
		});
	}
}
