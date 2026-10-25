#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/TharrnInteractable.h"
#include "TharrnWaterSource.generated.h"

class UStaticMeshComponent;

/** A seep or cistern the player can drink from. Untreated water and exposure come later (M2+). */
UCLASS()
class THARRN_API ATharrnWaterSource : public AActor, public ITharrnInteractable
{
	GENERATED_BODY()

public:
	ATharrnWaterSource();

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(EditAnywhere, Replicated) FString DisplayName = TEXT("Water seep");

	virtual FString GetInteractPrompt(const ATharrnCharacterBase* User) const override;
	virtual FString GetBlockedReason(const ATharrnCharacterBase* User) const override;
	virtual float GetInteractDuration(const ATharrnCharacterBase* User) const override;
	virtual bool Interact(ATharrnCharacterBase* User, FString& OutMessage) override;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
