#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TharrnInteractable.generated.h"

class ATharrnCharacterBase;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTharrnInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Anything a character can use: gather nodes, containers, water, later workstations, recruits and clues.
 * The user is a character, not a player, so recruits run the same interactions as the player does.
 */
class THARRN_API ITharrnInteractable
{
	GENERATED_BODY()

public:
	virtual FString GetInteractPrompt(const ATharrnCharacterBase* User) const { return TEXT("Use"); }
	/** Why User cannot interact now; empty means they can. Checked on the client for the prompt and again on the server. */
	virtual FString GetBlockedReason(const ATharrnCharacterBase* User) const { return FString(); }
	virtual float GetInteractDuration(const ATharrnCharacterBase* User) const { return 0.f; }
	/** Server only. Returns false with a reason if the interaction could not complete. */
	virtual bool Interact(ATharrnCharacterBase* User, FString& OutMessage) { return false; }
};
