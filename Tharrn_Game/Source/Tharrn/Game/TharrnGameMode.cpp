#include "Game/TharrnGameMode.h"

#include "Characters/TharrnPlayerCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/TharrnHUD.h"
#include "Game/TharrnPlayerController.h"
#include "Game/TharrnPlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Persistence/TharrnSaveSubsystem.h"
#include "Tharrn.h"
#include "TimerManager.h"
#include "World/TharrnDistrictBuilder.h"

ATharrnGameMode::ATharrnGameMode()
{
	DefaultPawnClass = ATharrnPlayerCharacter::StaticClass();
	PlayerControllerClass = ATharrnPlayerController::StaticClass();
	PlayerStateClass = ATharrnPlayerState::StaticClass();
	HUDClass = ATharrnHUD::StaticClass();
}

FString ATharrnGameMode::GetSaveSlot()
{
	FString Slot = TEXT("TharrnSlice");
	FParse::Value(FCommandLine::Get(), TEXT("TharrnSlot="), Slot);
	return Slot;
}

AActor* ATharrnGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	if (const ATharrnDistrictBuilder* Builder = ATharrnDistrictBuilder::Find(GetWorld()))
	{
		const FVector Arrival = Builder->GetArrivalPoint();
		for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), Arrival) < 50.f)
			{
				return *It;
			}
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return GetWorld()->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), Arrival, FRotator(0.f, Builder->GetArrivalYaw(), 0.f), Params);
	}
	return Super::ChoosePlayerStart_Implementation(Player);
}

void ATharrnGameMode::StartPlay()
{
	Super::StartPlay();
	if (!FParse::Param(FCommandLine::Get(), TEXT("TharrnFresh")))
	{
		// After the district has spawned its actors (their BeginPlay runs inside StartPlay).
		GetWorldTimerManager().SetTimer(AutoLoadTimer, this, &ATharrnGameMode::AutoLoad, 0.5f, false);
	}
}

void ATharrnGameMode::AutoLoad()
{
	UTharrnSaveSubsystem* Saves = GetWorld()->GetSubsystem<UTharrnSaveSubsystem>();
	const FString Slot = GetSaveSlot();
	if (Saves && Saves->HasSave(Slot))
	{
		UE_LOG(LogTharrn, Log, TEXT("Continuing campaign from slot %s"), *Slot);
		Saves->LoadFromSlot(Slot);
	}
}
