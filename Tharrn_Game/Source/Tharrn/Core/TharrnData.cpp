#include "Core/TharrnData.h"

#include "Tharrn.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	TSharedPtr<FJsonObject> ReadJsonFile(const FString& Path, TArray<FString>& Errors)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Errors.Add(FString::Printf(TEXT("Missing data file %s"), *Path));
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Errors.Add(FString::Printf(TEXT("Invalid JSON in %s: %s"), *Path, *Reader->GetErrorMessage()));
			return nullptr;
		}
		return Root;
	}

	template <typename RowType>
	void LoadRows(const FString& FileName, const TCHAR* Field, TMap<FName, RowType>& Out, TArray<FString>& Errors)
	{
		Out.Reset();
		const FString Path = FTharrnData::GetDataDir() / FileName;
		const TSharedPtr<FJsonObject> Root = ReadJsonFile(Path, Errors);
		if (!Root.IsValid())
		{
			return;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root->TryGetArrayField(Field, Rows) || !Rows)
		{
			Errors.Add(FString::Printf(TEXT("%s has no '%s' array"), *FileName, Field));
			return;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			RowType Row;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !FJsonObjectConverter::JsonObjectToUStruct(Object->ToSharedRef(), &Row, 0, 0))
			{
				Errors.Add(FString::Printf(TEXT("%s: unreadable row in '%s'"), *FileName, Field));
				continue;
			}
			if (Row.Id.IsNone() || Out.Contains(Row.Id))
			{
				Errors.Add(FString::Printf(TEXT("%s: missing or duplicate id '%s'"), *FileName, *Row.Id.ToString()));
				continue;
			}
			Out.Add(Row.Id, Row);
		}
	}

	FAutoConsoleCommand ReloadDataCommand(
		TEXT("Tharrn.ReloadData"),
		TEXT("Re-read Content/TharrnData/*.json"),
		FConsoleCommandDelegate::CreateLambda([]() { FTharrnData::Get().Reload(); }));
}

FTharrnData& FTharrnData::Get()
{
	static FTharrnData Instance;
	Instance.EnsureLoaded();
	return Instance;
}

FString FTharrnData::GetDataDir()
{
	return FPaths::ProjectContentDir() / TEXT("TharrnData");
}

void FTharrnData::EnsureLoaded()
{
	if (!bLoaded)
	{
		Reload();
	}
}

void FTharrnData::Reload()
{
	bLoaded = true;
	Errors.Reset();
	LoadRows(TEXT("items.json"), TEXT("Items"), Items, Errors);
	LoadRows(TEXT("resources.json"), TEXT("Resources"), Resources, Errors);
	LoadRows(TEXT("containers.json"), TEXT("Containers"), Containers, Errors);
	LoadRows(TEXT("creatures.json"), TEXT("Creatures"), Creatures, Errors);
	LoadRows(TEXT("effects.json"), TEXT("Effects"), Effects, Errors);

	Survival = FTharrnSurvivalTuning();
	if (const TSharedPtr<FJsonObject> Root = ReadJsonFile(GetDataDir() / TEXT("survival.json"), Errors))
	{
		FJsonObjectConverter::JsonObjectToUStruct(Root.ToSharedRef(), &Survival, 0, 0);
	}

	// Cross-references: every yield, drop and grant must name a known item.
	auto CheckItems = [this](const TArray<FTharrnItemAmount>& Amounts, const FString& Owner)
	{
		for (const FTharrnItemAmount& Amount : Amounts)
		{
			if (!Items.Contains(Amount.ItemId))
			{
				Errors.Add(FString::Printf(TEXT("%s names unknown item '%s'"), *Owner, *Amount.ItemId.ToString()));
			}
		}
	};
	for (const auto& Pair : Resources) { CheckItems(Pair.Value.Yields, TEXT("resource ") + Pair.Key.ToString()); }
	for (const auto& Pair : Creatures) { CheckItems(Pair.Value.Drops, TEXT("creature ") + Pair.Key.ToString()); }
	for (const auto& Pair : Containers) { CheckItems(Pair.Value.InitialContents, TEXT("container ") + Pair.Key.ToString()); }
	for (const auto& Pair : Items)
	{
		if (!Pair.Value.Effect.IsNone() && !Effects.Contains(Pair.Value.Effect))
		{
			Errors.Add(FString::Printf(TEXT("item %s names unknown effect '%s'"), *Pair.Key.ToString(), *Pair.Value.Effect.ToString()));
		}
	}

	for (const FString& Error : Errors)
	{
		UE_LOG(LogTharrn, Error, TEXT("TharrnData: %s"), *Error);
	}
	UE_LOG(LogTharrn, Log, TEXT("TharrnData: %d items, %d resources, %d containers, %d creatures, %d effects, %d errors"),
		Items.Num(), Resources.Num(), Containers.Num(), Creatures.Num(), Effects.Num(), Errors.Num());
}

FString FTharrnData::GetItemName(FName Id) const
{
	const FTharrnItemDef* Def = FindItem(Id);
	return Def ? Def->Name : Id.ToString();
}

float FTharrnData::GetItemWeight(FName Id) const
{
	const FTharrnItemDef* Def = FindItem(Id);
	return Def ? Def->Weight : 0.f;
}

bool FTharrnData::LoadLayout(const FString& FileName, FTharrnDistrictLayout& OutLayout) const
{
	TArray<FString> LayoutErrors;
	const TSharedPtr<FJsonObject> Root = ReadJsonFile(GetDataDir() / FileName, LayoutErrors);
	if (!Root.IsValid() || !FJsonObjectConverter::JsonObjectToUStruct(Root.ToSharedRef(), &OutLayout, 0, 0))
	{
		for (const FString& Error : LayoutErrors)
		{
			UE_LOG(LogTharrn, Error, TEXT("TharrnData: %s"), *Error);
		}
		return false;
	}
	return true;
}
