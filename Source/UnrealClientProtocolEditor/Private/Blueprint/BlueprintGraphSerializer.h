// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "NodeCode/NodeCodeTypes.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

class FBlueprintGraphSerializer
{
public:
	static FString Serialize(UBlueprint* Blueprint, const FString& ScopeName);

	static FNodeCodeGraphIR BuildIR(UBlueprint* Blueprint, const FString& ScopeName);

	static TArray<FString> ListScopes(UBlueprint* Blueprint);

	static UEdGraph* FindGraphByScope(UBlueprint* Blueprint, const FString& ScopeName);

private:

	static FNodeCodeGraphIR BuildIRFromGraph(UEdGraph* Graph, const FString& ScopeName);

	static void SerializeNodeProperties(
		UEdGraphNode* Node,
		TMap<FString, FString>& OutProperties);

	static void SerializePinDefaults(
		UEdGraphNode* Node,
		TMap<FString, FString>& OutProperties);

	static FString GetNodeClassName(UEdGraphNode* Node);

	static bool ShouldSkipNode(UEdGraphNode* Node);
};
