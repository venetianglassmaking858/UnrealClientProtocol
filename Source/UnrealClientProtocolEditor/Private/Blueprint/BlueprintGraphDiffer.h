// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "NodeCode/NodeCodeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

class FBlueprintGraphDiffer
{
public:
	static FNodeCodeDiffResult Apply(UBlueprint* Blueprint, const FString& ScopeName, const FString& GraphText);

private:
	static FNodeCodeDiffResult DiffAndApply(
		UBlueprint* Blueprint,
		UEdGraph* Graph,
		const FString& ScopeName,
		const FNodeCodeGraphIR& NewIR);

	static void MatchNodes(
		const FNodeCodeGraphIR& OldIR,
		const FNodeCodeGraphIR& NewIR,
		TMap<int32, int32>& OutNewToOld);

	static UEdGraphNode* CreateNodeFromIR(
		UEdGraph* Graph,
		UBlueprint* Blueprint,
		const FNodeCodeNodeIR& NodeIR);

	static void ApplyPropertyChanges(
		UEdGraphNode* Node,
		const TMap<FString, FString>& NewProperties,
		TArray<FString>& OutChanges);

	static void ApplyPinDefaults(
		UEdGraphNode* Node,
		const TMap<FString, FString>& Properties,
		TArray<FString>& OutChanges);

	static UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);
};
