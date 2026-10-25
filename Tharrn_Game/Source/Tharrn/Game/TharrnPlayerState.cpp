#include "Game/TharrnPlayerState.h"

#include "Net/UnrealNetwork.h"

void ATharrnPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnPlayerState, ProfileId);
}
