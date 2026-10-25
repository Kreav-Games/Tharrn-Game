#include "World/TharrnResourceNode.h"

#include "Characters/TharrnCharacterBase.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Persistence/TharrnPersistenceComponent.h"
#include "TimerManager.h"

ATharrnResourceNode::ATharrnResourceNode()
{
	bReplicates = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	Persistence = CreateDefaultSubobject<UTharrnPersistenceComponent>(TEXT("Persistence"));
}

void ATharrnResourceNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnResourceNode, ResourceDefId);
	DOREPLIFETIME(ATharrnResourceNode, ChargesRemaining);
}

const FTharrnResourceDef* ATharrnResourceNode::GetDef() const
{
	return FTharrnData::Get().FindResource(ResourceDefId);
}

void ATharrnResourceNode::BeginPlay()
{
	Super::BeginPlay();
	const FTharrnResourceDef* Def = GetDef();
	if (HasAuthority() && Def && ChargesRemaining < 0)
	{
		ChargesRemaining = Def->Charges;
	}
	RefreshVisual();
}

void ATharrnResourceNode::OnRep_State()
{
	RefreshVisual();
}

void ATharrnResourceNode::RefreshVisual()
{
	const FTharrnResourceDef* Def = GetDef();
	if (!Def)
	{
		return;
	}
	if (AppliedDef != ResourceDefId)
	{
		AppliedDef = ResourceDefId;
		Mesh->SetStaticMesh(TharrnStatics::BasicShape(Def->Shape));
		Mesh->SetWorldScale3D(Def->Size);
		TharrnStatics::ApplyColor(Mesh, Def->Color);
	}
	const bool bVisible = ChargesRemaining != 0;
	Mesh->SetVisibility(bVisible);
	Mesh->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}

FString ATharrnResourceNode::GetInteractPrompt(const ATharrnCharacterBase* User) const
{
	const FTharrnResourceDef* Def = GetDef();
	return Def ? FString::Printf(TEXT("Gather %s (%d left)"), *Def->Name, FMath::Max(0, ChargesRemaining)) : TEXT("Gather");
}

FString ATharrnResourceNode::GetBlockedReason(const ATharrnCharacterBase* User) const
{
	const FTharrnResourceDef* Def = GetDef();
	if (!Def || IsDepleted())
	{
		return TEXT("Depleted");
	}
	if (!Def->RequiredToolTag.IsNone() && (!User || User->GetActiveToolTag() != Def->RequiredToolTag))
	{
		return FString::Printf(TEXT("Needs a %s tool in hand"), *Def->RequiredToolTag.ToString());
	}
	if (User && User->Inventory && !User->Inventory->CanAcceptAll(Def->Yields))
	{
		return TEXT("No room in your pack");
	}
	return FString();
}

float ATharrnResourceNode::GetInteractDuration(const ATharrnCharacterBase* User) const
{
	const FTharrnResourceDef* Def = GetDef();
	return Def ? Def->GatherSeconds : 0.f;
}

bool ATharrnResourceNode::Interact(ATharrnCharacterBase* User, FString& OutMessage)
{
	const FTharrnResourceDef* Def = GetDef();
	OutMessage = GetBlockedReason(User);
	if (!HasAuthority() || !Def || !User || !OutMessage.IsEmpty())
	{
		return false;
	}
	TArray<FString> Parts;
	for (const FTharrnItemAmount& Yield : Def->Yields)
	{
		const int32 Added = User->Inventory->AddItem(Yield.ItemId, Yield.Count);
		Parts.Add(FString::Printf(TEXT("+%d %s"), Added, *FTharrnData::Get().GetItemName(Yield.ItemId)));
	}
	if (!Def->RequiredToolTag.IsNone())
	{
		User->WearActiveTool(Def->ToolWear);
	}
	ChargesRemaining = FMath::Max(0, ChargesRemaining - 1);
	if (ChargesRemaining == 0)
	{
		ScheduleRegrow(Def->RespawnSeconds);
	}
	RefreshVisual();
	OutMessage = FString::Join(Parts, TEXT(", "));
	return true;
}

void ATharrnResourceNode::ScheduleRegrow(float Seconds)
{
	GetWorldTimerManager().SetTimer(RegrowTimer, this, &ATharrnResourceNode::Regrow, FMath::Max(0.1f, Seconds), false);
}

void ATharrnResourceNode::Regrow()
{
	if (const FTharrnResourceDef* Def = GetDef(); Def && HasAuthority())
	{
		GetWorldTimerManager().ClearTimer(RegrowTimer);
		ChargesRemaining = Def->Charges;
		RefreshVisual();
	}
}

void ATharrnResourceNode::TharrnPreSave()
{
	RespawnRemaining = GetWorldTimerManager().IsTimerActive(RegrowTimer) ? GetWorldTimerManager().GetTimerRemaining(RegrowTimer) : 0.f;
}

void ATharrnResourceNode::TharrnPostLoad()
{
	GetWorldTimerManager().ClearTimer(RegrowTimer);
	if (ChargesRemaining == 0)
	{
		ScheduleRegrow(RespawnRemaining > 0.f ? RespawnRemaining : (GetDef() ? GetDef()->RespawnSeconds : 60.f));
	}
	RefreshVisual();
}
