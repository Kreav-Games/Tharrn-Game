#include "Game/TharrnPlayerController.h"

#include "Characters/TharrnPlayerCharacter.h"
#include "Game/TharrnGameMode.h"
#include "Game/TharrnPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Persistence/TharrnSaveSubsystem.h"

FString ATharrnPlayerController::LocalProfileId()
{
	FString Id;
	if (FParse::Value(FCommandLine::Get(), TEXT("TharrnProfile="), Id) && !Id.IsEmpty())
	{
		return Id;
	}
	if (!GConfig->GetString(TEXT("Tharrn"), TEXT("ProfileId"), Id, GGameUserSettingsIni) || Id.IsEmpty())
	{
		Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		GConfig->SetString(TEXT("Tharrn"), TEXT("ProfileId"), *Id, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
	}
	return Id;
}

void ATharrnPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		ServerSetProfileId(LocalProfileId());
	}
}

void ATharrnPlayerController::ServerSetProfileId_Implementation(const FString& Id)
{
	ATharrnPlayerState* State = GetPlayerState<ATharrnPlayerState>();
	// Prototype trust model: the client names its profile. A dedicated service would authenticate it.
	if (!State || Id.IsEmpty() || Id.Len() > 64 || !State->ProfileId.IsEmpty())
	{
		return;
	}
	State->ProfileId = Id;
	if (UTharrnSaveSubsystem* Saves = GetWorld()->GetSubsystem<UTharrnSaveSubsystem>())
	{
		Saves->ApplyPendingPlayer(Cast<ATharrnPlayerCharacter>(GetPawn()));
	}
}

void ATharrnPlayerController::NotifyPawn(const FString& Message)
{
	if (ATharrnPlayerCharacter* PlayerPawn = Cast<ATharrnPlayerCharacter>(GetPawn()))
	{
		PlayerPawn->Notify(Message);
	}
}

void ATharrnPlayerController::ServerRequestSave_Implementation()
{
	UTharrnSaveSubsystem* Saves = GetWorld()->GetSubsystem<UTharrnSaveSubsystem>();
	if (!IsLocalController())
	{
		NotifyPawn(TEXT("Only the host can save"));
		return;
	}
	const bool bSaved = Saves && Saves->SaveToSlot(ATharrnGameMode::GetSaveSlot());
	NotifyPawn(bSaved ? TEXT("Expedition saved") : TEXT("Save failed"));
}

void ATharrnPlayerController::ServerRequestLoad_Implementation()
{
	UTharrnSaveSubsystem* Saves = GetWorld()->GetSubsystem<UTharrnSaveSubsystem>();
	if (!IsLocalController())
	{
		NotifyPawn(TEXT("Only the host can load"));
		return;
	}
	const bool bLoaded = Saves && Saves->LoadFromSlot(ATharrnGameMode::GetSaveSlot());
	NotifyPawn(bLoaded ? TEXT("Expedition loaded") : (Saves ? Saves->GetLastError() : TEXT("Load failed")));
}

void ATharrnPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	const ATharrnPlayerCharacter* PlayerPawn = Cast<ATharrnPlayerCharacter>(GetPawn());
	const bool bWantPanel = PlayerPawn && (PlayerPawn->bInventoryOpen || PlayerPawn->OpenContainer != nullptr);
	if (bWantPanel != bPanelMode)
	{
		bPanelMode = bWantPanel;
		SetShowMouseCursor(bPanelMode);
		if (bPanelMode)
		{
			FInputModeGameAndUI Mode;
			Mode.SetHideCursorDuringCapture(false);
			SetInputMode(Mode);
		}
		else
		{
			SetInputMode(FInputModeGameOnly());
		}
	}
}
