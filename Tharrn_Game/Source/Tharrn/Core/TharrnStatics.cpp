#include "Core/TharrnStatics.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"

FGuid TharrnStatics::StableGuid(const FString& Key)
{
	FTCHARToUTF8 Utf8(*Key);
	FMD5 Md5;
	Md5.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	uint8 Digest[16];
	Md5.Final(Digest);
	uint32 Parts[4];
	FMemory::Memcpy(Parts, Digest, sizeof(Parts));
	return FGuid(Parts[0], Parts[1], Parts[2], Parts[3]);
}

UStaticMesh* TharrnStatics::BasicShape(const FString& Shape)
{
	const FString Name = Shape.IsEmpty() ? TEXT("Cube") : Shape;
	const FString Path = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), *Name, *Name);
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path);
	return Mesh ? Mesh : LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

UMaterialInstanceDynamic* TharrnStatics::ApplyColor(UPrimitiveComponent* Component, const FLinearColor& Color)
{
	if (!Component)
	{
		return nullptr;
	}
	static const TCHAR* BasePath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, BasePath);
	UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));
	if (!Mid && Base)
	{
		Mid = UMaterialInstanceDynamic::Create(Base, Component);
		Component->SetMaterial(0, Mid);
	}
	if (Mid)
	{
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
	}
	return Mid;
}

float TharrnStatics::ServerTime(const UWorld* World)
{
	if (!World)
	{
		return 0.f;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? static_cast<float>(GameState->GetServerWorldTimeSeconds()) : World->GetTimeSeconds();
}
