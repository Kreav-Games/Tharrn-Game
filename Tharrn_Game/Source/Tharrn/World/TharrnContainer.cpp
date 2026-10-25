#include "World/TharrnContainer.h"

#include "Characters/TharrnPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Engine/World.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/TharrnPersistenceComponent.h"
#include "TimerManager.h"

ATharrnContainer::ATharrnContainer()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetStaticMesh(TharrnStatics::BasicShape(TEXT("Cube")));
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Inventory = CreateDefaultSubobject<UTharrnInventoryComponent>(TEXT("Inventory"));
	Persistence = CreateDefaultSubobject<UTharrnPersistenceComponent>(TEXT("Persistence"));
}

void ATharrnContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnContainer, ContainerDefId);
	DOREPLIFETIME(ATharrnContainer, DisplayName);
	DOREPLIFETIME(ATharrnContainer, bDestroyWhenEmpty);
}

ATharrnContainer* ATharrnContainer::SpawnCache(UWorld* World, FName DefId, const FVector& Location, const FString& NameOverride)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ATharrnContainer* Cache = World->SpawnActorDeferred<ATharrnContainer>(ATharrnContainer::StaticClass(), FTransform(Location), nullptr, nullptr, Params.SpawnCollisionHandlingOverride);
	if (!Cache)
	{
		return nullptr;
	}
	Cache->ContainerDefId = DefId;
	Cache->DisplayName = NameOverride;
	Cache->Persistence->PersistentId = FGuid::NewGuid();
	Cache->Persistence->bRuntimeSpawned = true;
	Cache->Persistence->DebugKey = DefId.ToString();
	Cache->FinishSpawning(FTransform(Location));
	return Cache;
}

void ATharrnContainer::BeginPlay()
{
	Super::BeginPlay();
	ApplyDef();
	if (HasAuthority())
	{
		Inventory->OnChanged.AddDynamic(this, &ATharrnContainer::HandleInventoryChanged);
		const FTharrnContainerDef* Def = FTharrnData::Get().FindContainer(ContainerDefId);
		if (Def && !bInitialContentsGranted)
		{
			bInitialContentsGranted = true;
			for (const FTharrnItemAmount& Amount : Def->InitialContents)
			{
				Inventory->AddItem(Amount.ItemId, Amount.Count);
			}
		}
	}
}

void ATharrnContainer::OnRep_Def()
{
	ApplyDef();
}

void ATharrnContainer::ApplyDef()
{
	const FTharrnContainerDef* Def = FTharrnData::Get().FindContainer(ContainerDefId);
	if (!Def || AppliedDef == ContainerDefId)
	{
		return;
	}
	AppliedDef = ContainerDefId;
	if (HasAuthority())
	{
		Inventory->MaxSlots = Def->Slots;
		bDestroyWhenEmpty = Def->bDestroyWhenEmpty;
		if (DisplayName.IsEmpty())
		{
			DisplayName = Def->Name;
		}
	}
	Mesh->SetWorldScale3D(Def->Size);
	TharrnStatics::ApplyColor(Mesh, Def->Color);
}

void ATharrnContainer::HandleInventoryChanged()
{
	if (bDestroyWhenEmpty && Inventory->IsEmpty() && HasAuthority())
	{
		// Deferred so the transfer that emptied it finishes first.
		GetWorldTimerManager().SetTimerForNextTick([WeakThis = TWeakObjectPtr<ATharrnContainer>(this)]()
		{
			if (WeakThis.IsValid() && WeakThis->Inventory->IsEmpty())
			{
				WeakThis->Destroy();
			}
		});
	}
}

FString ATharrnContainer::GetInteractPrompt(const ATharrnCharacterBase* User) const
{
	return FString::Printf(TEXT("Open %s (%d items)"), *DisplayName, Inventory->TotalItemCount());
}

bool ATharrnContainer::Interact(ATharrnCharacterBase* User, FString& OutMessage)
{
	ATharrnPlayerCharacter* Player = Cast<ATharrnPlayerCharacter>(User);
	if (!Player)
	{
		OutMessage = TEXT("Only players open containers directly");
		return false;
	}
	Player->OpenContainerFor(this);
	return true;
}

void ATharrnContainer::TharrnPostLoad()
{
	AppliedDef = NAME_None;
	ApplyDef();
	Inventory->NotifyChanged();
}
