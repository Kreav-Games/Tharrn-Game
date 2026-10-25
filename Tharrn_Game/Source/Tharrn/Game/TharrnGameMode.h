#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TharrnGameMode.generated.h"

/**
 * Slice game mode. Places arrivals at the district's arrival point, continues the campaign save on start
 * (unless -TharrnFresh), and hands joining players their personal record.
 */
UCLASS()
class THARRN_API ATharrnGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATharrnGameMode();

	static FString GetSaveSlot();

	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;

protected:
	void AutoLoad();
	FTimerHandle AutoLoadTimer;
};
