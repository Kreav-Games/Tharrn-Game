#include "Characters/TharrnCharacterBase.h"

#include "Combat/TharrnCombatComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/TharrnData.h"
#include "Core/TharrnStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/TharrnInventoryComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Survival/TharrnVitalsComponent.h"

ATharrnCharacterBase::ATharrnCharacterBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	Vitals = CreateDefaultSubobject<UTharrnVitalsComponent>(TEXT("Vitals"));
	Combat = CreateDefaultSubobject<UTharrnCombatComponent>(TEXT("Combat"));
	Inventory = CreateDefaultSubobject<UTharrnInventoryComponent>(TEXT("Inventory"));

	GetCapsuleComponent()->InitCapsuleSize(40.f, 90.f);
	GetMesh()->SetVisibility(false);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	auto MakePart = [this](const TCHAR* Name, const TCHAR* Shape, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(TharrnStatics::BasicShape(Shape));
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	};
	BodyMesh = MakePart(TEXT("BodyMesh"), TEXT("Cylinder"), GetCapsuleComponent());
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, -20.f));
	BodyMesh->SetRelativeScale3D(FVector(0.7f, 0.7f, 1.3f));
	HeadMesh = MakePart(TEXT("HeadMesh"), TEXT("Sphere"), GetCapsuleComponent());
	HeadMesh->SetRelativeLocation(FVector(8.f, 0.f, 68.f));
	HeadMesh->SetRelativeScale3D(FVector(0.45f));

	WeaponPivot = CreateDefaultSubobject<USceneComponent>(TEXT("WeaponPivot"));
	WeaponPivot->SetupAttachment(GetCapsuleComponent());
	WeaponPivot->SetRelativeLocation(FVector(10.f, 38.f, 25.f));
	WeaponMesh = MakePart(TEXT("WeaponMesh"), TEXT("Cube"), WeaponPivot);
	WeaponMesh->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	WeaponMesh->SetRelativeScale3D(FVector(0.08f, 0.08f, 0.8f));

	TelegraphMesh = MakePart(TEXT("TelegraphMesh"), TEXT("Cylinder"), GetCapsuleComponent());
	TelegraphMesh->SetVisibility(false);
	TelegraphMesh->SetCastShadow(false);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 600.f, 0.f);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->BrakingDecelerationWalking = 2000.f;
	Move->JumpZVelocity = 500.f;
	Move->AirControl = 0.3f;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;
}

void ATharrnCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ATharrnCharacterBase, bWantsSprint);
}

void ATharrnCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	BodyMaterial = TharrnStatics::ApplyColor(BodyMesh, BaseColor);
	HeadMaterial = TharrnStatics::ApplyColor(HeadMesh, BaseColor * 1.3f);
	TharrnStatics::ApplyColor(WeaponMesh, FLinearColor(0.35f, 0.33f, 0.3f));
	TharrnStatics::ApplyColor(TelegraphMesh, FLinearColor(1.f, 0.05f, 0.02f));
	ShownColor = BaseColor;
	if (HasAuthority())
	{
		Vitals->OnDied.AddDynamic(this, &ATharrnCharacterBase::HandleDeath);
	}
}

bool ATharrnCharacterBase::IsHostileTo(const ATharrnCharacterBase* Other) const
{
	if (!Other || Other == this)
	{
		return false;
	}
	switch (Faction)
	{
	case ETharrnFaction::Expedition: return Other->Faction != ETharrnFaction::Expedition;
	case ETharrnFaction::Hostile: return Other->Faction == ETharrnFaction::Expedition;
	default: return false;
	}
}

FTharrnMeleeStats ATharrnCharacterBase::GetAttackStats() const
{
	return FTharrnData::Get().GetSurvival().Unarmed;
}

FString ATharrnCharacterBase::GetDisplayName() const
{
	return GetName();
}

float ATharrnCharacterBase::GetBodyRadius() const
{
	return GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleRadius() : 40.f;
}

bool ATharrnCharacterBase::IsDead() const
{
	return Vitals && Vitals->bDead;
}

void ATharrnCharacterBase::MulticastCombatEvent_Implementation(ETharrnHitOutcome Outcome, float Damage)
{
	LastEventOutcome = Outcome;
	LastEventDamage = Damage;
	LastEventTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void ATharrnCharacterBase::HandleDeath(AActor* Killer)
{
	Combat->CancelAttack();
	Combat->SetBlocking(false);
	bWantsSprint = false;
}

float ATharrnCharacterBase::ComputeMoveSpeed() const
{
	if (IsDead() || Combat->bStaggered)
	{
		return 0.f;
	}
	const float Ratio = Inventory ? Inventory->GetWeightRatio() : 0.f;
	float Speed = (bWantsSprint && Ratio <= 1.f && !Combat->bBlocking) ? RunSpeed : WalkSpeed;
	if (Combat->bBlocking)
	{
		Speed *= 0.4f;
	}
	if (Combat->Phase == ETharrnAttackPhase::Windup || Combat->Phase == ETharrnAttackPhase::Active)
	{
		Speed *= 0.35f;
	}
	if (Ratio > 1.f)
	{
		Speed *= FMath::GetMappedRangeValueClamped(FVector2D(1.f, 1.5f), FVector2D(0.7f, 0.3f), Ratio);
	}
	return Speed;
}

void ATharrnCharacterBase::SetBodyColor(const FLinearColor& Color)
{
	if (!Color.Equals(ShownColor, 0.01f))
	{
		ShownColor = Color;
		if (BodyMaterial)
		{
			BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color);
		}
	}
}

void ATharrnCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Same rule on server and owning client keeps movement prediction close.
	GetCharacterMovement()->MaxWalkSpeed = ComputeMoveSpeed();
	UpdateVisuals(DeltaSeconds);
}

void ATharrnCharacterBase::UpdateVisuals(float DeltaSeconds)
{
	const ETharrnAttackPhase Phase = Combat->Phase;
	const float Alpha = Combat->GetPhaseAlpha();

	// Weapon arm: raised back in windup, swung through when active, settles in recovery.
	float TargetPitch = Combat->bBlocking ? -55.f : -15.f;
	switch (Phase)
	{
	case ETharrnAttackPhase::Windup: TargetPitch = FMath::Lerp(-15.f, 75.f, Alpha); break;
	case ETharrnAttackPhase::Active: TargetPitch = FMath::Lerp(75.f, -110.f, Alpha); break;
	case ETharrnAttackPhase::Recovery: TargetPitch = FMath::Lerp(-110.f, -15.f, Alpha); break;
	default: break;
	}
	WeaponPitch = Phase == ETharrnAttackPhase::None ? FMath::FInterpTo(WeaponPitch, TargetPitch, DeltaSeconds, 12.f) : TargetPitch;
	WeaponPivot->SetRelativeRotation(FRotator(WeaponPitch, 0.f, Combat->bBlocking ? 70.f : 0.f));

	FLinearColor Color = BaseColor;
	if (IsDead())
	{
		Color = BaseColor * 0.25f;
	}
	else if (Combat->bStaggered)
	{
		Color = FLinearColor(0.95f, 0.8f, 0.1f);
	}
	else if (Phase == ETharrnAttackPhase::Windup && bShowTelegraph)
	{
		Color = FMath::Lerp(BaseColor, FLinearColor(1.f, 0.15f, 0.05f), Alpha);
	}
	const float SinceHit = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) - LastEventTime;
	if (SinceHit < 0.12f && LastEventDamage > 0.f)
	{
		Color = FLinearColor::White;
	}
	SetBodyColor(Color);

	const bool bTelegraph = bShowTelegraph && (Phase == ETharrnAttackPhase::Windup || Phase == ETharrnAttackPhase::Active);
	TelegraphMesh->SetVisibility(bTelegraph);
	if (bTelegraph)
	{
		const FTharrnMeleeStats& Attack = Combat->GetCurrentAttack();
		const float Grow = Phase == ETharrnAttackPhase::Windup ? FMath::Lerp(0.35f, 1.f, Alpha) : 1.f;
		const float Diameter = (Attack.Range * 2.f) * Grow;
		const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		TelegraphMesh->SetRelativeLocation(FVector(Attack.Range * 0.5f, 0.f, -HalfHeight + 2.f));
		TelegraphMesh->SetRelativeScale3D(FVector(Diameter / 100.f, Diameter / 100.f, 0.02f));
	}
}
