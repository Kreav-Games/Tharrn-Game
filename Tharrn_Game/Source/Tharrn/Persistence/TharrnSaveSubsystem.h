#pragma once

#include "CoreMinimal.h"
#include "Persistence/TharrnSaveGame.h"
#include "Subsystems/WorldSubsystem.h"
#include "TharrnSaveSubsystem.generated.h"

class ATharrnPlayerCharacter;

/**
 * Saves and restores the shared world and each player's personal state. Server only.
 * Serialisation is generic: any UPROPERTY(SaveGame) on a persistent actor or its components is written,
 * so new systems persist by tagging fields rather than by adding save code.
 */
UCLASS()
class THARRN_API UTharrnSaveSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	bool SaveToSlot(const FString& Slot);
	bool LoadFromSlot(const FString& Slot);
	bool HasSave(const FString& Slot) const;

	void RegisterDestroyed(const FGuid& Id);
	bool HasWorldFlag(FName Flag) const { return WorldFlags.Contains(Flag); }
	void SetWorldFlag(FName Flag) { WorldFlags.AddUnique(Flag); }

	/** Applies a saved personal record to a player who joins or reconnects after a load. */
	void ApplyPendingPlayer(ATharrnPlayerCharacter* Player);

	static FString ProfileIdOf(const ATharrnPlayerCharacter* Player);
	static void WriteActor(AActor* Actor, TArray<uint8>& OutData);
	static void ReadActor(AActor* Actor, const TArray<uint8>& Data);

	FString GetLastError() const { return LastError; }
	int32 GetLoadCount() const { return LoadCount; }

protected:
	void ApplyPlayerRecord(ATharrnPlayerCharacter* Player, const FTharrnPlayerRecord& Record);

	TSet<FGuid> DestroyedAuthored;
	TArray<FName> WorldFlags;
	/** Records of players not present at save or load time, kept so their progress is never dropped. */
	TMap<FString, FTharrnPlayerRecord> PendingPlayers;
	FString LastError;
	int32 LoadCount = 0;
	bool bLoading = false;
};
