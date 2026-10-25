#pragma once

#include "CoreMinimal.h"
#include "Core/TharrnTypes.h"

/**
 * The gameplay definition registry. Reads Content/TharrnData/*.json once, on first use.
 * Plain C++ so automation tests, editor tools and the game share it without a world.
 */
class THARRN_API FTharrnData
{
public:
	static FTharrnData& Get();

	/** Re-reads every file (console: Tharrn.ReloadData). Live actors keep the values they copied. */
	void Reload();

	const FTharrnItemDef* FindItem(FName Id) const { return Items.Find(Id); }
	const FTharrnResourceDef* FindResource(FName Id) const { return Resources.Find(Id); }
	const FTharrnContainerDef* FindContainer(FName Id) const { return Containers.Find(Id); }
	const FTharrnCreatureDef* FindCreature(FName Id) const { return Creatures.Find(Id); }
	const FTharrnEffectDef* FindEffect(FName Id) const { return Effects.Find(Id); }
	const FTharrnSurvivalTuning& GetSurvival() const { return Survival; }

	FString GetItemName(FName Id) const;
	float GetItemWeight(FName Id) const;

	bool LoadLayout(const FString& FileName, FTharrnDistrictLayout& OutLayout) const;
	static FString GetDataDir();

	const TArray<FString>& GetErrors() const { return Errors; }

	TMap<FName, FTharrnItemDef> Items;
	TMap<FName, FTharrnResourceDef> Resources;
	TMap<FName, FTharrnContainerDef> Containers;
	TMap<FName, FTharrnCreatureDef> Creatures;
	TMap<FName, FTharrnEffectDef> Effects;
	FTharrnSurvivalTuning Survival;

private:
	FTharrnData() = default;
	void EnsureLoaded();

	bool bLoaded = false;
	TArray<FString> Errors;
};
