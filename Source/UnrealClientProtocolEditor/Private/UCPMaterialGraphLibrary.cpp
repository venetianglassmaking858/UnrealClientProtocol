// MIT License - Copyright (c) 2025 Italink

#include "UCPMaterialGraphLibrary.h"
#include "MaterialGraphSerializer.h"
#include "MaterialGraphDiffer.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MaterialEditingLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

static UObject* LoadMaterialAsset(const FString& AssetPath)
{
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	return Asset;
}

static FString MakeErrorJson(const FString& Error)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), false);
	Root->SetStringField(TEXT("error"), Error);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}

FString UMaterialGraphLibrary::ReadGraph(const FString& AssetPath, const FString& ScopeName)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		return FMaterialGraphSerializer::Serialize(Material, ScopeName);
	}

	if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		return FMaterialGraphSerializer::Serialize(MaterialFunction, ScopeName);
	}

	return MakeErrorJson(FString::Printf(TEXT("Asset is not a Material or MaterialFunction: %s"), *AssetPath));
}

FString UMaterialGraphLibrary::WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	FMGDiffResult Result;

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		Result = FMaterialGraphDiffer::Apply(Material, ScopeName, GraphText);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		Result = FMaterialGraphDiffer::Apply(MaterialFunction, ScopeName, GraphText);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset is not a Material or MaterialFunction: %s"), *AssetPath));
	}

	return FMaterialGraphDiffer::DiffResultToJson(Result);
}

FString UMaterialGraphLibrary::Relayout(const FString& AssetPath)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(MaterialFunction);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset is not a Material or MaterialFunction: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}

FString UMaterialGraphLibrary::ListScopes(const FString& AssetPath)
{
	UObject* Asset = LoadMaterialAsset(AssetPath);
	if (!Asset)
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	TArray<FString> Scopes;

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		Scopes = FMaterialGraphSerializer::ListScopes(Material);
	}
	else if (UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset))
	{
		Scopes = FMaterialGraphSerializer::ListScopes(MaterialFunction);
	}
	else
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset is not a Material or MaterialFunction: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> ScopeValues;
	for (const FString& Scope : Scopes)
	{
		ScopeValues.Add(MakeShared<FJsonValueString>(Scope));
	}
	Root->SetArrayField(TEXT("scopes"), ScopeValues);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
