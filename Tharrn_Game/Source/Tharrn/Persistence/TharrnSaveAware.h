#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "TharrnSaveAware.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UTharrnSaveAware : public UInterface
{
	GENERATED_BODY()
};

/** Optional hooks around the generic SaveGame-property serialisation, for state that needs converting (timers). */
class THARRN_API ITharrnSaveAware
{
	GENERATED_BODY()

public:
	virtual void TharrnPreSave() {}
	virtual void TharrnPostLoad() {}
};
