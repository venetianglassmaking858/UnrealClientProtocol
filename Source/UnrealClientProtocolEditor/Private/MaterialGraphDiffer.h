// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "MaterialGraphSerializer.h"

class UMaterial;
class UMaterialFunction;
class UMaterialExpression;

struct FMGDiffResult
{
	bool bSuccess = false;
	TArray<FString> NodesAdded;
	TArray<FString> NodesRemoved;
	TArray<FString> NodesModified;
	TArray<FString> LinksAdded;
	TArray<FString> LinksRemoved;
	bool bRelayout = false;
};

class FMaterialGraphDiffer
{
public:
	static FMGDiffResult Apply(UMaterial* Material, const FString& ScopeName, const FString& GraphText);
	static FMGDiffResult Apply(UMaterialFunction* MaterialFunction, const FString& ScopeName, const FString& GraphText);

	static FString DiffResultToJson(const FMGDiffResult& Result);

private:
	static FMGGraphIR ParseText(const FString& GraphText);

	static FMGDiffResult DiffAndApply(
		UMaterial* Material,
		UMaterialFunction* MaterialFunction,
		const FString& ScopeName,
		const FMGGraphIR& NewIR);

	static void MatchNodes(
		const FMGGraphIR& OldIR,
		const FMGGraphIR& NewIR,
		TMap<int32, int32>& OutNewToOld);

	static void ApplyPropertyChanges(
		UMaterialExpression* Expression,
		const TMap<FString, FString>& NewProperties,
		TArray<FString>& OutChanges);

	static EMaterialProperty FindMaterialPropertyByName(UMaterial* Material, const FString& Name);
};
