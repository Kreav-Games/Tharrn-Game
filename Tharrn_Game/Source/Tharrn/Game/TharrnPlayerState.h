#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TharrnPlayerState.generated.h"

UCLASS()
class THARRN_API ATharrnPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Stable per-person id the save keys personal progress on; survives reconnects and new connections. */
	UPROPERTY(BlueprintReadOnly, Replicated) FString ProfileId;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
