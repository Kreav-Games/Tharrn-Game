#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TharrnPlayerController.generated.h"

UCLASS()
class THARRN_API ATharrnPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** This machine's profile id: -TharrnProfile=Name, else a GUID kept in the user's settings. */
	static FString LocalProfileId();

	UFUNCTION(Server, Reliable) void ServerSetProfileId(const FString& Id);
	UFUNCTION(Server, Reliable) void ServerRequestSave();
	UFUNCTION(Server, Reliable) void ServerRequestLoad();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

protected:
	void NotifyPawn(const FString& Message);
	bool bPanelMode = false;
};
