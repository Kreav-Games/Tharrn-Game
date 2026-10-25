#include "World/TharrnDistrictBuilder.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/TextRenderComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Persistence/TharrnPersistenceComponent.h"
#include "Tharrn.h"
#include "World/TharrnContainer.h"
#include "World/TharrnCreatureSpawner.h"
#include "World/TharrnResourceNode.h"
#include "World/TharrnWaterSource.h"

ATharrnDistrictBuilder::ATharrnDistrictBuilder()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	for (int32 Index = 0; Index < PaletteSize; ++Index)
	{
		UInstancedStaticMeshComponent* Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(*FString::Printf(TEXT("Palette%d"), Index));
		Mesh->SetupAttachment(GetRootComponent());
		Mesh->SetStaticMesh(TharrnStatics::BasicShape(TEXT("Cube")));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCanEverAffectNavigation(true);
		PaletteMeshes.Add(Mesh);
	}
}

ATharrnDistrictBuilder* ATharrnDistrictBuilder::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<ATharrnDistrictBuilder> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

bool ATharrnDistrictBuilder::LoadLayout()
{
	FTharrnDistrictLayout Loaded;
	bLayoutLoaded = FTharrnData::Get().LoadLayout(LayoutFile, Loaded);
	if (bLayoutLoaded)
	{
		Layout = MoveTemp(Loaded);
	}
	return bLayoutLoaded;
}

void ATharrnDistrictBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (LoadLayout())
	{
		BuildGeometry();
	}
}

void ATharrnDistrictBuilder::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Loaded early so the game mode can place the first player at the arrival point.
	LoadLayout();
}

void ATharrnDistrictBuilder::BeginPlay()
{
	Super::BeginPlay();
	if (!bLayoutLoaded && !LoadLayout())
	{
		UE_LOG(LogTharrn, Error, TEXT("District layout %s could not be loaded"), *LayoutFile);
		return;
	}
	BuildGeometry();
	BuildLighting();
	BuildLabels();
	if (HasAuthority())
	{
		SpawnGameplay();
	}
}

int32 ATharrnDistrictBuilder::PaletteIndex(FName Material) const
{
	const int32 Index = Layout.Palette.IndexOfByPredicate([Material](const FTharrnPaletteEntry& E) { return E.Name == Material; });
	return Index == INDEX_NONE ? 0 : FMath::Min(Index, PaletteSize - 1);
}

void ATharrnDistrictBuilder::AddBlock(const FVector& Center, const FVector& Size, float Yaw, FName Material)
{
	if (Size.X <= 1.f || Size.Y <= 1.f || Size.Z <= 1.f)
	{
		return;
	}
	PaletteMeshes[PaletteIndex(Material)]->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Center, Size / 100.f));
	++BlockCount;
}

void ATharrnDistrictBuilder::BuildGeometry()
{
	BlockCount = 0;
	for (int32 Index = 0; Index < PaletteMeshes.Num(); ++Index)
	{
		UInstancedStaticMeshComponent* Mesh = PaletteMeshes[Index];
		Mesh->ClearInstances();
		const FLinearColor Color = Layout.Palette.IsValidIndex(Index) ? Layout.Palette[Index].Color : FLinearColor::Gray;
		TharrnStatics::ApplyColor(Mesh, Color);
	}
	const float T = Layout.WallThickness;
	for (int32 RoomIndex = 0; RoomIndex < Layout.Rooms.Num(); ++RoomIndex)
	{
		const FTharrnDistrictRoom& Room = Layout.Rooms[RoomIndex];
		const FVector2D Size = Room.Max - Room.Min;
		const FVector2D Mid = (Room.Min + Room.Max) * 0.5f;
		AddBlock(FVector(Mid.X, Mid.Y, Room.FloorZ - T * 0.5f), FVector(Size.X + T * 2.f, Size.Y + T * 2.f, T), 0.f, Room.Floor);
		if (Room.bCeiling)
		{
			AddBlock(FVector(Mid.X, Mid.Y, Room.FloorZ + Room.Height + T * 0.5f), FVector(Size.X + T * 2.f, Size.Y + T * 2.f, T), 0.f, Room.Wall);
		}
		BuildRoomWalls(RoomIndex);
	}
	for (const FTharrnDistrictBlock& Block : Layout.Blocks)
	{
		AddBlock(Block.Center, Block.Size, Block.Yaw, Block.Material);
	}
}

void ATharrnDistrictBuilder::BuildRoomWalls(int32 RoomIndex)
{
	const FTharrnDistrictRoom& Room = Layout.Rooms[RoomIndex];
	const float T = Layout.WallThickness;
	const float Tol = 2.f;

	// Each edge: 0 = -X, 1 = +X, 2 = -Y, 3 = +Y. Openings are spans where a neighbouring room extends past the edge.
	for (int32 Edge = 0; Edge < 4; ++Edge)
	{
		const bool bAlongY = Edge < 2;
		const float Line = Edge == 0 ? Room.Min.X : Edge == 1 ? Room.Max.X : Edge == 2 ? Room.Min.Y : Room.Max.Y;
		const float SpanMin = bAlongY ? Room.Min.Y : Room.Min.X;
		const float SpanMax = bAlongY ? Room.Max.Y : Room.Max.X;
		const float Outward = (Edge == 0 || Edge == 2) ? -1.f : 1.f;

		struct FOpening { float Min; float Max; float Top; };
		TArray<FOpening> Openings;
		for (int32 OtherIndex = 0; OtherIndex < Layout.Rooms.Num(); ++OtherIndex)
		{
			if (OtherIndex == RoomIndex)
			{
				continue;
			}
			const FTharrnDistrictRoom& Other = Layout.Rooms[OtherIndex];
			const float OtherNear = bAlongY ? (Outward < 0.f ? Other.Max.X : Other.Min.X) : (Outward < 0.f ? Other.Max.Y : Other.Min.Y);
			const float OtherFar = bAlongY ? (Outward < 0.f ? Other.Min.X : Other.Max.X) : (Outward < 0.f ? Other.Min.Y : Other.Max.Y);
			// The neighbour must reach the edge line from inside or on it and continue outward beyond it.
			const bool bTouches = Outward > 0.f ? (OtherNear <= Line + Tol && OtherFar > Line + Tol) : (OtherNear >= Line - Tol && OtherFar < Line - Tol);
			if (!bTouches)
			{
				continue;
			}
			float OMin = FMath::Max(SpanMin, bAlongY ? Other.Min.Y : Other.Min.X);
			float OMax = FMath::Min(SpanMax, bAlongY ? Other.Max.Y : Other.Max.X);
			if (OMax - OMin > Tol)
			{
				// An opening that reaches a corner also swallows that corner's wall overlap.
				OMin = OMin <= SpanMin + Tol ? SpanMin - T : OMin;
				OMax = OMax >= SpanMax - Tol ? SpanMax + T : OMax;
				Openings.Add({OMin, OMax, Other.FloorZ + Other.Height});
			}
		}
		Openings.Sort([](const FOpening& A, const FOpening& B) { return A.Min < B.Min; });

		const float WallCenterLine = Line + Outward * T * 0.5f;
		auto EmitSpan = [&](float A, float B, float ZMin, float ZMax)
		{
			if (B - A <= 1.f || ZMax - ZMin <= 1.f)
			{
				return;
			}
			const float Mid = (A + B) * 0.5f;
			const FVector Center = bAlongY ? FVector(WallCenterLine, Mid, (ZMin + ZMax) * 0.5f) : FVector(Mid, WallCenterLine, (ZMin + ZMax) * 0.5f);
			const FVector Size = bAlongY ? FVector(T, B - A, ZMax - ZMin) : FVector(B - A, T, ZMax - ZMin);
			AddBlock(Center, Size, 0.f, Room.Wall);
		};

		const float Bottom = Room.FloorZ;
		const float Top = Room.FloorZ + Room.Height;
		// Walls run one thickness past the corners so adjacent edges close without gaps.
		float Cursor = SpanMin - T;
		for (const FOpening& Opening : Openings)
		{
			EmitSpan(Cursor, Opening.Min, Bottom, Top);
			// A lintel closes the gap above a lower neighbour.
			EmitSpan(Opening.Min, Opening.Max, FMath::Max(Bottom, Opening.Top), Top);
			Cursor = FMath::Max(Cursor, Opening.Max);
		}
		EmitSpan(Cursor, SpanMax + T, Bottom, Top);
	}
}

void ATharrnDistrictBuilder::BuildLighting()
{
	auto AddPoint = [this](const FVector& Location, float Intensity, float Radius, const FLinearColor& Color, bool bShadows)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetupAttachment(GetRootComponent());
		Light->SetRelativeLocation(Location);
		Light->SetIntensityUnits(ELightUnits::Lumens);
		Light->SetIntensity(Intensity);
		Light->SetAttenuationRadius(Radius);
		Light->SetLightColor(Color);
		Light->SetCastShadows(bShadows);
		Light->RegisterComponent();
	};
	for (const FTharrnDistrictRoom& Room : Layout.Rooms)
	{
		if (Room.LightIntensity <= 0.f)
		{
			continue;
		}
		const FVector2D Mid = (Room.Min + Room.Max) * 0.5f;
		const float Radius = (Room.Max - Room.Min).Size() * 0.75f + 400.f;
		// Only the big rooms cast shadows; tunnel lights stay cheap.
		AddPoint(FVector(Mid.X, Mid.Y, Room.FloorZ + Room.Height * 0.8f), Room.LightIntensity, Radius, Room.LightColor, !Room.Label.IsEmpty());
	}
	for (const FTharrnDistrictLight& Light : Layout.Lights)
	{
		AddPoint(Light.Location, Light.Intensity, Light.Radius, Light.Color, false);
	}
	// Keep caves reading as caves: auto exposure would otherwise lift them to a flat mid-grey.
	UPostProcessComponent* Grade = NewObject<UPostProcessComponent>(this);
	Grade->SetupAttachment(GetRootComponent());
	Grade->bUnbound = true;
	Grade->Settings.bOverride_AutoExposureBias = true;
	Grade->Settings.AutoExposureBias = Layout.ExposureBias;
	Grade->RegisterComponent();
	if (Layout.FillLightIntensity > 0.f)
	{
		// Shadowless fill so greybox shapes never sink into pure black; reads as faint bioluminescence.
		UDirectionalLightComponent* Fill = NewObject<UDirectionalLightComponent>(this);
		Fill->SetupAttachment(GetRootComponent());
		Fill->SetRelativeRotation(FRotator(-55.f, 35.f, 0.f));
		Fill->SetIntensity(Layout.FillLightIntensity);
		Fill->SetLightColor(FLinearColor(0.55f, 0.65f, 0.8f));
		Fill->SetCastShadows(false);
		Fill->RegisterComponent();
	}
	UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(GetRootComponent());
	Fog->SetFogDensity(0.02f);
	Fog->SetFogHeightFalloff(0.3f);
	Fog->SetFogInscatteringColor(FLinearColor(0.05f, 0.07f, 0.09f));
	Fog->RegisterComponent();
}

void ATharrnDistrictBuilder::BuildLabels()
{
	auto AddLabel = [this](const FString& Text, const FVector& Location, float Yaw, float Size, const FColor& Color)
	{
		UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this);
		Label->SetupAttachment(GetRootComponent());
		Label->SetRelativeLocation(Location);
		Label->SetRelativeRotation(FRotator(0.f, Yaw, 0.f));
		Label->SetText(FText::FromString(Text));
		Label->SetWorldSize(Size);
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetVerticalAlignment(EVRTA_TextCenter);
		Label->SetTextRenderColor(Color);
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->RegisterComponent();
	};
	for (const FTharrnDistrictRoom& Room : Layout.Rooms)
	{
		if (!Room.Label.IsEmpty())
		{
			const FVector2D Mid = (Room.Min + Room.Max) * 0.5f;
			AddLabel(Room.Label, FVector(Mid.X, Mid.Y, Room.FloorZ + FMath::Min(Room.Height - 150.f, 520.f)), Room.LabelYaw, 90.f, FColor(255, 214, 150));
		}
	}
	for (const FTharrnDistrictSpawn& Spawn : Layout.Spawns)
	{
		if (Spawn.Type == TEXT("Label"))
		{
			AddLabel(Spawn.Label, Spawn.Location, Spawn.Yaw, 45.f, FColor(200, 220, 255));
		}
	}
}

void ATharrnDistrictBuilder::SpawnGameplay()
{
	if (bGameplaySpawned)
	{
		return;
	}
	bGameplaySpawned = true;
	UWorld* World = GetWorld();
	const FTransform Base = GetActorTransform();
	for (const FTharrnDistrictSpawn& Spawn : Layout.Spawns)
	{
		const FTransform Where(FRotator(0.f, Spawn.Yaw, 0.f), Base.TransformPosition(Spawn.Location));
		const FGuid Id = TharrnStatics::StableGuid(Layout.Name + TEXT("/") + Spawn.Id);
		AActor* Actor = nullptr;
		if (Spawn.Type == TEXT("Resource"))
		{
			ATharrnResourceNode* Node = World->SpawnActorDeferred<ATharrnResourceNode>(ATharrnResourceNode::StaticClass(), Where);
			Node->ResourceDefId = Spawn.Def;
			Node->Persistence->PersistentId = Id;
			Node->Persistence->DebugKey = Spawn.Id;
			Node->FinishSpawning(Where);
			Actor = Node;
		}
		else if (Spawn.Type == TEXT("Container"))
		{
			ATharrnContainer* Container = World->SpawnActorDeferred<ATharrnContainer>(ATharrnContainer::StaticClass(), Where);
			Container->ContainerDefId = Spawn.Def;
			Container->DisplayName = Spawn.Label;
			Container->Persistence->PersistentId = Id;
			Container->Persistence->DebugKey = Spawn.Id;
			Container->FinishSpawning(Where);
			Actor = Container;
		}
		else if (Spawn.Type == TEXT("Water"))
		{
			ATharrnWaterSource* Water = World->SpawnActorDeferred<ATharrnWaterSource>(ATharrnWaterSource::StaticClass(), Where);
			if (!Spawn.Label.IsEmpty())
			{
				Water->DisplayName = Spawn.Label;
			}
			Water->FinishSpawning(Where);
			Actor = Water;
		}
		else if (Spawn.Type == TEXT("Spawner"))
		{
			ATharrnCreatureSpawner* Spawner = World->SpawnActorDeferred<ATharrnCreatureSpawner>(ATharrnCreatureSpawner::StaticClass(), Where);
			Spawner->CreatureDefId = Spawn.Def;
			Spawner->MaxAlive = FMath::Max(1, Spawn.Count);
			Spawner->StableKey = Spawn.Id;
			Spawner->FinishSpawning(Where);
			Actor = Spawner;
		}
		if (Actor)
		{
			Spawned.Add(Spawn.Id, Actor);
			SpawnedDefs.Add(Spawn.Id, Spawn.Def);
		}
	}
	UE_LOG(LogTharrn, Log, TEXT("District '%s': %d blocks, %d gameplay actors"), *Layout.Name, BlockCount, Spawned.Num());
}

AActor* ATharrnDistrictBuilder::FindSpawned(const FString& Id) const
{
	const TWeakObjectPtr<AActor>* Found = Spawned.Find(Id);
	return Found ? Found->Get() : nullptr;
}

TArray<AActor*> ATharrnDistrictBuilder::FindSpawnedOfDef(FName Def) const
{
	TArray<AActor*> Result;
	for (const auto& Pair : SpawnedDefs)
	{
		if (Pair.Value == Def)
		{
			if (AActor* Actor = FindSpawned(Pair.Key))
			{
				Result.Add(Actor);
			}
		}
	}
	return Result;
}
