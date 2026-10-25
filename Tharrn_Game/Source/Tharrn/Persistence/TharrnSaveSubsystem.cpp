#include "Persistence/TharrnSaveSubsystem.h"

#include "Characters/TharrnPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TharrnPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Persistence/TharrnPersistenceComponent.h"
#include "Persistence/TharrnSaveAware.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Tharrn.h"

namespace
{
	void CallPreSave(AActor* Actor)
	{
		if (ITharrnSaveAware* Aware = Cast<ITharrnSaveAware>(Actor))
		{
			Aware->TharrnPreSave();
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (ITharrnSaveAware* Aware = Cast<ITharrnSaveAware>(Component))
			{
				Aware->TharrnPreSave();
			}
		}
	}

	void CallPostLoad(AActor* Actor)
	{
		if (ITharrnSaveAware* Aware = Cast<ITharrnSaveAware>(Actor))
		{
			Aware->TharrnPostLoad();
		}
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (ITharrnSaveAware* Aware = Cast<ITharrnSaveAware>(Component))
			{
				Aware->TharrnPostLoad();
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs SaveCommand(TEXT("Tharrn.Save"), TEXT("Tharrn.Save [Slot]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTharrnSaveSubsystem* Saves = World ? World->GetSubsystem<UTharrnSaveSubsystem>() : nullptr)
			{
				Saves->SaveToSlot(Args.Num() > 0 ? Args[0] : TEXT("TharrnSlice"));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs LoadCommand(TEXT("Tharrn.Load"), TEXT("Tharrn.Load [Slot]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UTharrnSaveSubsystem* Saves = World ? World->GetSubsystem<UTharrnSaveSubsystem>() : nullptr)
			{
				Saves->LoadFromSlot(Args.Num() > 0 ? Args[0] : TEXT("TharrnSlice"));
			}
		}));
}

void UTharrnSaveSubsystem::WriteActor(AActor* Actor, TArray<uint8>& OutData)
{
	OutData.Reset();
	FMemoryWriter Memory(OutData, true);
	FObjectAndNameAsStringProxyArchive Archive(Memory, true);
	Archive.ArIsSaveGame = true;
	Archive.ArNoDelta = true;
	Actor->Serialize(Archive);

	// Components by name, so adding or reordering components never shifts another's data.
	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	int32 Count = Components.Num();
	Archive << Count;
	for (UActorComponent* Component : Components)
	{
		FString Name = Component->GetName();
		TArray<uint8> Bytes;
		FMemoryWriter ComponentMemory(Bytes, true);
		FObjectAndNameAsStringProxyArchive ComponentArchive(ComponentMemory, true);
		ComponentArchive.ArIsSaveGame = true;
		ComponentArchive.ArNoDelta = true;
		Component->Serialize(ComponentArchive);
		Archive << Name;
		Archive << Bytes;
	}
}

void UTharrnSaveSubsystem::ReadActor(AActor* Actor, const TArray<uint8>& Data)
{
	FMemoryReader Memory(Data, true);
	FObjectAndNameAsStringProxyArchive Archive(Memory, true);
	Archive.ArIsSaveGame = true;
	Archive.ArNoDelta = true;
	Actor->Serialize(Archive);

	TMap<FString, UActorComponent*> ByName;
	for (UActorComponent* Component : Actor->GetComponents())
	{
		ByName.Add(Component->GetName(), Component);
	}
	int32 Count = 0;
	Archive << Count;
	for (int32 Index = 0; Index < Count && !Archive.IsError(); ++Index)
	{
		FString Name;
		TArray<uint8> Bytes;
		Archive << Name;
		Archive << Bytes;
		if (UActorComponent** Component = ByName.Find(Name))
		{
			FMemoryReader ComponentMemory(Bytes, true);
			FObjectAndNameAsStringProxyArchive ComponentArchive(ComponentMemory, true);
			ComponentArchive.ArIsSaveGame = true;
			ComponentArchive.ArNoDelta = true;
			(*Component)->Serialize(ComponentArchive);
		}
	}
}

FString UTharrnSaveSubsystem::ProfileIdOf(const ATharrnPlayerCharacter* Player)
{
	const ATharrnPlayerState* State = Player ? Player->GetPlayerState<ATharrnPlayerState>() : nullptr;
	return State && !State->ProfileId.IsEmpty() ? State->ProfileId : FString();
}

bool UTharrnSaveSubsystem::HasSave(const FString& Slot) const
{
	return UGameplayStatics::DoesSaveGameExist(Slot, 0);
}

void UTharrnSaveSubsystem::RegisterDestroyed(const FGuid& Id)
{
	if (!bLoading)
	{
		DestroyedAuthored.Add(Id);
	}
}

bool UTharrnSaveSubsystem::SaveToSlot(const FString& Slot)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		LastError = TEXT("Only the host saves");
		return false;
	}
	UTharrnSaveGame* Save = Cast<UTharrnSaveGame>(UGameplayStatics::CreateSaveGameObject(UTharrnSaveGame::StaticClass()));
	Save->MapName = World->GetMapName();
	Save->SavedAt = FDateTime::UtcNow();
	Save->PlaySeconds = World->GetTimeSeconds();
	Save->DestroyedAuthored = DestroyedAuthored.Array();
	Save->WorldFlags = WorldFlags;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		const UTharrnPersistenceComponent* Persistence = Actor ? Actor->FindComponentByClass<UTharrnPersistenceComponent>() : nullptr;
		if (!Persistence || !Persistence->PersistentId.IsValid() || Actor->IsActorBeingDestroyed())
		{
			continue;
		}
		CallPreSave(Actor);
		FTharrnActorRecord& Record = Save->Actors.AddDefaulted_GetRef();
		Record.Id = Persistence->PersistentId;
		Record.Class = FSoftClassPath(Actor->GetClass());
		Record.Transform = Actor->GetActorTransform();
		Record.bRuntimeSpawned = Persistence->bRuntimeSpawned;
		Record.DebugKey = Persistence->DebugKey;
		WriteActor(Actor, Record.Data);
	}

	TMap<FString, FTharrnPlayerRecord> Players = PendingPlayers;
	for (TActorIterator<ATharrnPlayerCharacter> It(World); It; ++It)
	{
		ATharrnPlayerCharacter* Player = *It;
		const FString Profile = ProfileIdOf(Player);
		if (Profile.IsEmpty())
		{
			continue;
		}
		CallPreSave(Player);
		FTharrnPlayerRecord Record;
		Record.ProfileId = Profile;
		Record.DisplayName = Player->GetDisplayName();
		Record.Transform = Player->GetActorTransform();
		WriteActor(Player, Record.Data);
		Players.Add(Profile, MoveTemp(Record));
	}
	Players.GenerateValueArray(Save->Players);

	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	LastError = bSaved ? FString() : TEXT("SaveGameToSlot failed");
	UE_LOG(LogTharrn, Log, TEXT("Save '%s': %s, %d actors, %d players, %d destroyed"), *Slot, bSaved ? TEXT("ok") : TEXT("FAILED"),
		Save->Actors.Num(), Save->Players.Num(), Save->DestroyedAuthored.Num());
	return bSaved;
}

bool UTharrnSaveSubsystem::LoadFromSlot(const FString& Slot)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		LastError = TEXT("Only the host loads");
		return false;
	}
	UTharrnSaveGame* Save = Cast<UTharrnSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!Save)
	{
		LastError = FString::Printf(TEXT("No save in slot %s"), *Slot);
		return false;
	}
	if (Save->Version > UTharrnSaveGame::CurrentVersion)
	{
		LastError = FString::Printf(TEXT("Save version %d is newer than this build (%d)"), Save->Version, UTharrnSaveGame::CurrentVersion);
		return false;
	}
	TGuardValue<bool> LoadingGuard(bLoading, true);

	TMap<FGuid, AActor*> Live;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		const UTharrnPersistenceComponent* Persistence = It->FindComponentByClass<UTharrnPersistenceComponent>();
		if (Persistence && Persistence->PersistentId.IsValid() && !It->IsActorBeingDestroyed())
		{
			Live.Add(Persistence->PersistentId, *It);
		}
	}

	// Roll the world back to the save: runtime actors absent from it go, authored ones it marks destroyed go.
	TSet<FGuid> InSave;
	for (const FTharrnActorRecord& Record : Save->Actors)
	{
		InSave.Add(Record.Id);
	}
	DestroyedAuthored = TSet<FGuid>(Save->DestroyedAuthored);
	for (const auto& Pair : Live)
	{
		const UTharrnPersistenceComponent* Persistence = Pair.Value->FindComponentByClass<UTharrnPersistenceComponent>();
		const bool bStaleRuntime = Persistence->bRuntimeSpawned && !InSave.Contains(Pair.Key);
		if (bStaleRuntime || DestroyedAuthored.Contains(Pair.Key))
		{
			Pair.Value->Destroy();
		}
	}

	TArray<AActor*> Touched;
	for (const FTharrnActorRecord& Record : Save->Actors)
	{
		if (DestroyedAuthored.Contains(Record.Id))
		{
			continue;
		}
		AActor* Actor = Live.FindRef(Record.Id);
		if (Actor && Actor->IsActorBeingDestroyed())
		{
			Actor = nullptr;
		}
		if (!Actor && Record.bRuntimeSpawned)
		{
			UClass* Class = Record.Class.TryLoadClass<AActor>();
			if (!Class)
			{
				UE_LOG(LogTharrn, Warning, TEXT("Load: class %s for %s is gone"), *Record.Class.ToString(), *Record.DebugKey);
				continue;
			}
			Actor = World->SpawnActorDeferred<AActor>(Class, Record.Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (UTharrnPersistenceComponent* Persistence = Actor ? Actor->FindComponentByClass<UTharrnPersistenceComponent>() : nullptr)
			{
				Persistence->PersistentId = Record.Id;
				Persistence->bRuntimeSpawned = true;
			}
			// Restored before BeginPlay so one-time grants see the saved flags.
			ReadActor(Actor, Record.Data);
			Actor->FinishSpawning(Record.Transform);
		}
		else if (Actor)
		{
			if (Record.bRuntimeSpawned)
			{
				Actor->SetActorTransform(Record.Transform, false, nullptr, ETeleportType::TeleportPhysics);
			}
			ReadActor(Actor, Record.Data);
		}
		if (Actor)
		{
			Touched.Add(Actor);
		}
	}
	for (AActor* Actor : Touched)
	{
		CallPostLoad(Actor);
	}

	WorldFlags = Save->WorldFlags;
	PendingPlayers.Reset();
	for (const FTharrnPlayerRecord& Record : Save->Players)
	{
		PendingPlayers.Add(Record.ProfileId, Record);
	}
	for (TActorIterator<ATharrnPlayerCharacter> It(World); It; ++It)
	{
		ApplyPendingPlayer(*It);
	}
	++LoadCount;
	LastError.Reset();
	UE_LOG(LogTharrn, Log, TEXT("Load '%s': %d actors restored, %d player records"), *Slot, Touched.Num(), Save->Players.Num());
	return true;
}

void UTharrnSaveSubsystem::ApplyPendingPlayer(ATharrnPlayerCharacter* Player)
{
	const FString Profile = ProfileIdOf(Player);
	if (Profile.IsEmpty())
	{
		return;
	}
	FTharrnPlayerRecord Record;
	if (PendingPlayers.RemoveAndCopyValue(Profile, Record))
	{
		ApplyPlayerRecord(Player, Record);
	}
}

void UTharrnSaveSubsystem::ApplyPlayerRecord(ATharrnPlayerCharacter* Player, const FTharrnPlayerRecord& Record)
{
	Player->CancelInteract();
	Player->CloseContainer();
	ReadActor(Player, Record.Data);
	Player->TeleportTo(Record.Transform.GetLocation(), Record.Transform.Rotator(), false, true);
	CallPostLoad(Player);
	UE_LOG(LogTharrn, Log, TEXT("Load: restored player %s"), *Record.ProfileId);
}
