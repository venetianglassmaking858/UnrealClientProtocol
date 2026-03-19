// MIT License - Copyright (c) 2025 Italink

#include "Blueprint/BlueprintGraphEditingLibrary.h"
#include "Blueprint/BlueprintGraphSerializer.h"
#include "Blueprint/BlueprintGraphDiffer.h"
#include "NodeCode/NodeCodeTextFormat.h"

#include "Engine/Blueprint.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlueprintGraphEditing, Log, All);

static UBlueprint* LoadBlueprintAsset(const FString& AssetPath)
{
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	return Cast<UBlueprint>(Asset);
}

FString UBlueprintGraphEditingLibrary::ReadGraph(const FString& AssetPath, const FString& ScopeName)
{
	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintGraphEditing, Error, TEXT("ReadGraph: Blueprint not found: %s"), *AssetPath);
		return FString();
	}

	return FBlueprintGraphSerializer::Serialize(Blueprint, ScopeName);
}

FString UBlueprintGraphEditingLibrary::WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText)
{
	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintGraphEditing, Error, TEXT("WriteGraph: Blueprint not found: %s"), *AssetPath);
		return FString();
	}

	FNodeCodeDiffResult Result = FBlueprintGraphDiffer::Apply(Blueprint, ScopeName, GraphText);
	return FNodeCodeTextFormat::DiffResultToJson(Result);
}

FString UBlueprintGraphEditingLibrary::ListScopes(const FString& AssetPath)
{
	UBlueprint* Blueprint = LoadBlueprintAsset(AssetPath);
	if (!Blueprint)
	{
		UE_LOG(LogBlueprintGraphEditing, Error, TEXT("ListScopes: Blueprint not found: %s"), *AssetPath);
		return FString();
	}

	TArray<FString> Scopes = FBlueprintGraphSerializer::ListScopes(Blueprint);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ScopeValues;
	for (const FString& Scope : Scopes)
	{
		ScopeValues.Add(MakeShared<FJsonValueString>(Scope));
	}
	Root->SetArrayField(TEXT("scopes"), ScopeValues);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
