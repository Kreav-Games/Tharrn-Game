#pragma once

#include "CoreMinimal.h"

class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class UStaticMesh;
class UWorld;

namespace TharrnStatics
{
	/** A GUID derived from a stable text key, so authored actors keep their identity across sessions. */
	THARRN_API FGuid StableGuid(const FString& Key);

	/** /Engine/BasicShapes mesh by name: Cube, Sphere, Cylinder, Cone, Plane. */
	THARRN_API UStaticMesh* BasicShape(const FString& Shape);

	/** Gives a component a flat-colour greybox material (BasicShapeMaterial's Color parameter). */
	THARRN_API UMaterialInstanceDynamic* ApplyColor(UPrimitiveComponent* Component, const FLinearColor& Color);

	/** Server world time on every machine (GameState-synchronised). */
	THARRN_API float ServerTime(const UWorld* World);
}
