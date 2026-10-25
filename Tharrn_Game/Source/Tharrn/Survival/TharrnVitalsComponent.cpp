#include "Survival/TharrnVitalsComponent.h"

#include "Core/TharrnData.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UTharrnVitalsComponent::UTharrnVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UTharrnVitalsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UTharrnVitalsComponent, MaxHealth);
	DOREPLIFETIME(UTharrnVitalsComponent, MaxStamina);
	DOREPLIFETIME(UTharrnVitalsComponent, MaxFood);
	DOREPLIFETIME(UTharrnVitalsComponent, MaxWater);
	DOREPLIFETIME(UTharrnVitalsComponent, Health);
	DOREPLIFETIME(UTharrnVitalsComponent, Stamina);
	DOREPLIFETIME(UTharrnVitalsComponent, Food);
	DOREPLIFETIME(UTharrnVitalsComponent, Water);
	DOREPLIFETIME(UTharrnVitalsComponent, bDead);
	DOREPLIFETIME(UTharrnVitalsComponent, Effects);
}

bool UTharrnVitalsComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return !Owner || Owner->HasAuthority();
}

void UTharrnVitalsComponent::InitPools(float InMaxHealth, float InMaxStamina, bool bInHasNeeds)
{
	MaxHealth = InMaxHealth;
	MaxStamina = InMaxStamina;
	Health = MaxHealth;
	Stamina = MaxStamina;
	bHasNeeds = bInHasNeeds;
}

float UTharrnVitalsComponent::ApplyDamage(float Amount, AActor* Instigator)
{
	if (!HasAuthority() || bDead || Amount <= 0.f)
	{
		return 0.f;
	}
	const float Applied = FMath::Min(Amount, Health);
	Health -= Applied;
	OnDamaged.Broadcast(Applied, Instigator);
	if (Health <= 0.f)
	{
		Health = 0.f;
		bDead = true;
		OnDied.Broadcast(Instigator);
	}
	return Applied;
}

void UTharrnVitalsComponent::Heal(float Amount)
{
	if (HasAuthority() && !bDead)
	{
		Health = FMath::Clamp(Health + Amount, 0.f, MaxHealth);
	}
}

bool UTharrnVitalsComponent::TrySpendStamina(float Amount)
{
	if (!HasAuthority() || bDead || Stamina < Amount)
	{
		return false;
	}
	Stamina -= Amount;
	LastStaminaSpendTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	return true;
}

void UTharrnVitalsComponent::DrainStamina(float Amount)
{
	if (HasAuthority() && Amount > 0.f)
	{
		Stamina = FMath::Max(0.f, Stamina - Amount);
		LastStaminaSpendTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	}
}

void UTharrnVitalsComponent::Consume(const FTharrnItemDef& Item)
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	Food = FMath::Clamp(Food + Item.Food, 0.f, MaxFood);
	Water = FMath::Clamp(Water + Item.Water, 0.f, MaxWater);
	if (Item.Health > 0.f)
	{
		Heal(Item.Health);
	}
	else if (Item.Health < 0.f)
	{
		ApplyDamage(-Item.Health, GetOwner());
	}
	if (!Item.Effect.IsNone())
	{
		AddEffect(Item.Effect);
	}
}

void UTharrnVitalsComponent::AddWater(float Amount)
{
	if (HasAuthority() && !bDead)
	{
		Water = FMath::Clamp(Water + Amount, 0.f, MaxWater);
	}
}

void UTharrnVitalsComponent::AddEffect(FName EffectId)
{
	const FTharrnEffectDef* Def = FTharrnData::Get().FindEffect(EffectId);
	if (!HasAuthority() || !Def)
	{
		return;
	}
	// Re-eating refreshes the duration; effects of the same kind never stack.
	for (FTharrnActiveEffect& Active : Effects)
	{
		if (Active.EffectId == EffectId)
		{
			Active.Remaining = Def->Duration;
			return;
		}
	}
	Effects.Add({EffectId, Def->Duration});
}

bool UTharrnVitalsComponent::HasEffect(FName EffectId) const
{
	return Effects.ContainsByPredicate([EffectId](const FTharrnActiveEffect& E) { return E.EffectId == EffectId; });
}

void UTharrnVitalsComponent::Recover(float HealthFraction, float MinFood, float MinWater)
{
	if (!HasAuthority())
	{
		return;
	}
	bDead = false;
	Health = FMath::Max(1.f, MaxHealth * HealthFraction);
	Stamina = MaxStamina;
	Food = FMath::Max(Food, MinFood);
	Water = FMath::Max(Water, MinWater);
	Effects.Reset();
}

void UTharrnVitalsComponent::OnRep_Dead()
{
}

void UTharrnVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!HasAuthority() || bDead)
	{
		return;
	}
	const FTharrnSurvivalTuning& Tuning = FTharrnData::Get().GetSurvival();

	// Stamina recovers smoothly after a short pause.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastStaminaSpendTime >= Tuning.StaminaRegenDelay && Stamina < MaxStamina)
	{
		float Multiplier = RegenScale;
		for (const FTharrnActiveEffect& Active : Effects)
		{
			if (const FTharrnEffectDef* Def = FTharrnData::Get().FindEffect(Active.EffectId))
			{
				Multiplier *= Def->StaminaRegenMultiplier;
			}
		}
		Stamina = FMath::Min(MaxStamina, Stamina + Tuning.StaminaRegenPerSecond * Multiplier * DeltaTime);
	}

	// Needs and effects advance in one-second steps rather than every frame.
	NeedsAccumulator += DeltaTime;
	if (NeedsAccumulator >= 1.f)
	{
		const float Seconds = NeedsAccumulator;
		NeedsAccumulator = 0.f;
		TickNeeds(Seconds);
	}
}

void UTharrnVitalsComponent::TickNeeds(float Seconds)
{
	const FTharrnSurvivalTuning& Tuning = FTharrnData::Get().GetSurvival();
	float Regen = 0.f;
	for (int32 Index = Effects.Num() - 1; Index >= 0; --Index)
	{
		FTharrnActiveEffect& Active = Effects[Index];
		if (const FTharrnEffectDef* Def = FTharrnData::Get().FindEffect(Active.EffectId))
		{
			Regen += Def->HealthRegenPerSecond;
		}
		Active.Remaining -= Seconds;
		if (Active.Remaining <= 0.f)
		{
			Effects.RemoveAt(Index);
		}
	}
	if (!bHasNeeds)
	{
		Heal(Regen * Seconds);
		return;
	}
	Food = FMath::Max(0.f, Food - Tuning.FoodDrainPerSecond * Seconds);
	Water = FMath::Max(0.f, Water - Tuning.WaterDrainPerSecond * Seconds);

	float Loss = 0.f;
	if (Food <= 0.f)
	{
		Loss += Tuning.StarvingHealthLossPerSecond;
	}
	if (Water <= 0.f)
	{
		Loss += Tuning.DehydratedHealthLossPerSecond;
	}
	if (Loss > 0.f)
	{
		ApplyDamage(Loss * Seconds, GetOwner());
	}
	else if (Food >= Tuning.HealthRegenNeedsThreshold && Water >= Tuning.HealthRegenNeedsThreshold)
	{
		Heal((Tuning.HealthRegenPerSecond + Regen) * Seconds);
	}
}
