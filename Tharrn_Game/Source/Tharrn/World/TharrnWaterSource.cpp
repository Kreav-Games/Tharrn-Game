#include "World/TharrnWaterSource.h"

#include "Characters/TharrnCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Net/UnrealNetwork.h"
#include "Survival/TharrnVitalsComponent.h"

ATharrnWaterSource::ATharrnWaterSource()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetStaticMesh(TharrnStatics::BasicShape(TEXT("Cylinder")));
	Mesh->SetWorldScale3D(FVector(2.2f, 2.2f, 0.25f));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
}

void ATharrnWaterSource::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnWaterSource, DisplayName);
}

void ATharrnWaterSource::BeginPlay()
{
	Super::BeginPlay();
	TharrnStatics::ApplyColor(Mesh, FLinearColor(0.05f, 0.25f, 0.6f));
}

FString ATharrnWaterSource::GetInteractPrompt(const ATharrnCharacterBase* User) const
{
	return FString::Printf(TEXT("Drink from %s"), *DisplayName);
}

FString ATharrnWaterSource::GetBlockedReason(const ATharrnCharacterBase* User) const
{
	return (User && User->Vitals && User->Vitals->Water >= User->Vitals->MaxWater - 0.5f) ? TEXT("Not thirsty") : FString();
}

float ATharrnWaterSource::GetInteractDuration(const ATharrnCharacterBase* User) const
{
	return FTharrnData::Get().GetSurvival().DrinkSeconds;
}

bool ATharrnWaterSource::Interact(ATharrnCharacterBase* User, FString& OutMessage)
{
	OutMessage = GetBlockedReason(User);
	if (!User || !OutMessage.IsEmpty())
	{
		return false;
	}
	const float Before = User->Vitals->Water;
	User->Vitals->AddWater(FTharrnData::Get().GetSurvival().WaterPerDrink);
	OutMessage = FString::Printf(TEXT("Water +%.0f"), User->Vitals->Water - Before);
	return true;
}
