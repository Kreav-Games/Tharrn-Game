#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "TharrnSaveGame.generated.h"

/** One persistent actor: its SaveGame properties and those of its components, as bytes. */
USTRUCT()
struct FTharrnActorRecord
{
	GENERATED_BODY()

	UPROPERTY() FGuid Id;
	UPROPERTY() FSoftClassPath Class;
	UPROPERTY() FTransform Transform;
	UPROPERTY() bool bRuntimeSpawned = false;
	UPROPERTY() FString DebugKey;
	UPROPERTY() TArray<uint8> Data;
};

/** One player's personal state, keyed by a stable profile id rather than a connection. */
USTRUCT()
struct FTharrnPlayerRecord
{
	GENERATED_BODY()

	UPROPERTY() FString ProfileId;
	UPROPERTY() FString DisplayName;
	UPROPERTY() FTransform Transform;
	UPROPERTY() TArray<uint8> Data;
};

/**
 * Version 1 layout. Shared world state (settlement, containers, nodes, caches, flags) lives in Actors and WorldFlags;
 * personal progress lives in Players. Reservations, open interactions and live creatures are deliberately not saved:
 * on load, unfinished interactions are dropped and reservations are re-made by whoever needs them.
 */
UCLASS()
class THARRN_API UTharrnSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() FString MapName;
	UPROPERTY() FDateTime SavedAt;
	UPROPERTY() float PlaySeconds = 0.f;
	UPROPERTY() TArray<FTharrnActorRecord> Actors;
	UPROPERTY() TArray<FGuid> DestroyedAuthored;
	UPROPERTY() TArray<FTharrnPlayerRecord> Players;
	UPROPERTY() TArray<FName> WorldFlags;
};
