// MIT License - Copyright (c) 2025 Italink

#include "Blueprint/BlueprintGraphSerializer.h"
#include "NodeCode/NodeCodeTextFormat.h"
#include "NodeCode/NodeCodePropertyUtils.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Knot.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UnrealType.h"

FString FBlueprintGraphSerializer::Serialize(UBlueprint* Blueprint, const FString& ScopeName)
{
	FNodeCodeGraphIR IR = BuildIR(Blueprint, ScopeName);
	return FNodeCodeTextFormat::IRToText(IR);
}

FNodeCodeGraphIR FBlueprintGraphSerializer::BuildIR(UBlueprint* Blueprint, const FString& ScopeName)
{
	if (!Blueprint)
	{
		return {};
	}

	UEdGraph* Graph = FindGraphByScope(Blueprint, ScopeName);
	if (!Graph)
	{
		return {};
	}

	return BuildIRFromGraph(Graph, ScopeName);
}

TArray<FString> FBlueprintGraphSerializer::ListScopes(UBlueprint* Blueprint)
{
	TArray<FString> Scopes;
	if (!Blueprint)
	{
		return Scopes;
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph)
		{
			Scopes.Add(NodeCodeUtils::EncodeSpaces(Graph->GetName()));
		}
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph)
		{
			Scopes.Add(FString::Printf(TEXT("Function:%s"), *NodeCodeUtils::EncodeSpaces(Graph->GetName())));
		}
	}

	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph)
		{
			Scopes.Add(FString::Printf(TEXT("Macro:%s"), *NodeCodeUtils::EncodeSpaces(Graph->GetName())));
		}
	}

	return Scopes;
}

UEdGraph* FBlueprintGraphSerializer::FindGraphByScope(UBlueprint* Blueprint, const FString& ScopeName)
{
	if (!Blueprint)
	{
		return nullptr;
	}

	FString Prefix, GraphName;
	if (ScopeName.Split(TEXT(":"), &Prefix, &GraphName))
	{
		auto SearchGraphArray = [&GraphName](const TArray<TObjectPtr<UEdGraph>>& Graphs) -> UEdGraph*
		{
			for (const TObjectPtr<UEdGraph>& Graph : Graphs)
			{
				if (Graph && NodeCodeUtils::MatchName(GraphName, Graph->GetName()))
				{
					return Graph.Get();
				}
			}
			return nullptr;
		};

		if (Prefix == TEXT("Function"))
		{
			return SearchGraphArray(Blueprint->FunctionGraphs);
		}
		if (Prefix == TEXT("Macro"))
		{
			return SearchGraphArray(Blueprint->MacroGraphs);
		}
		return nullptr;
	}

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && NodeCodeUtils::MatchName(ScopeName, Graph->GetName()))
		{
			return Graph;
		}
	}

	if (ScopeName.IsEmpty() && Blueprint->UbergraphPages.Num() > 0)
	{
		return Blueprint->UbergraphPages[0];
	}

	return nullptr;
}

FNodeCodeGraphIR FBlueprintGraphSerializer::BuildIRFromGraph(UEdGraph* Graph, const FString& ScopeName)
{
	FNodeCodeGraphIR IR;
	IR.ScopeName = ScopeName;

	TMap<UEdGraphNode*, int32> NodeToIndex;
	int32 NodeCounter = 0;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || ShouldSkipNode(Node))
		{
			continue;
		}

		FNodeCodeNodeIR NodeIR;
		NodeIR.Index = NodeCounter;
		NodeIR.SourceObject = Node;
		NodeIR.Guid = Node->NodeGuid;
		NodeIR.ClassName = GetNodeClassName(Node);

		SerializeNodeProperties(Node, NodeIR.Properties);
		SerializePinDefaults(Node, NodeIR.Properties);

		NodeToIndex.Add(Node, NodeCounter);
		IR.Nodes.Add(MoveTemp(NodeIR));
		NodeCounter++;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || ShouldSkipNode(Node))
		{
			continue;
		}

		int32* ToIdx = NodeToIndex.Find(Node);
		if (!ToIdx)
		{
			continue;
		}

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				continue;
			}

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode())
				{
					continue;
				}

				UEdGraphNode* FromNode = LinkedPin->GetOwningNode();

				while (UK2Node_Knot* Knot = Cast<UK2Node_Knot>(FromNode))
				{
					UEdGraphPin* KnotInput = Knot->GetInputPin();
					if (KnotInput && KnotInput->LinkedTo.Num() > 0)
					{
						LinkedPin = KnotInput->LinkedTo[0];
						FromNode = LinkedPin->GetOwningNode();
					}
					else
					{
						FromNode = nullptr;
						break;
					}
				}

				if (!FromNode)
				{
					continue;
				}

				int32* FromIdx = NodeToIndex.Find(FromNode);
				if (!FromIdx)
				{
					continue;
				}

				FNodeCodeLinkIR Link;
				Link.FromNodeIndex = *FromIdx;
				Link.FromOutputName = NodeCodeUtils::EncodeSpaces(LinkedPin->PinName.ToString());
				Link.ToNodeIndex = *ToIdx;
				Link.ToInputName = NodeCodeUtils::EncodeSpaces(Pin->PinName.ToString());
				Link.bToGraphOutput = false;
				IR.Links.Add(MoveTemp(Link));
			}
		}
	}

	return IR;
}

bool FBlueprintGraphSerializer::ShouldSkipNode(UEdGraphNode* Node)
{
	if (Cast<UEdGraphNode_Comment>(Node))
	{
		return true;
	}
	if (Cast<UK2Node_Knot>(Node))
	{
		return true;
	}
	return false;
}

FString FBlueprintGraphSerializer::GetNodeClassName(UEdGraphNode* Node)
{
	if (!Node)
	{
		return FString();
	}

	UClass* NodeClass = Node->GetClass();

	// Semantic encoding only for exact base classes.
	// Subclasses (e.g. UK2Node_CallParentFunction, UK2Node_ComponentBoundEvent)
	// fall through to the raw class name at the bottom, and reflection handles their properties.

	if (NodeClass == UK2Node_CallFunction::StaticClass())
	{
		UK2Node_CallFunction* CallFunc = CastChecked<UK2Node_CallFunction>(Node);
		FName FuncName = CallFunc->FunctionReference.GetMemberName();
		UClass* FuncClass = CallFunc->FunctionReference.GetMemberParentClass();
		FString EncodedFuncName = NodeCodeUtils::EncodeSpaces(FuncName.ToString());
		if (FuncClass && !CallFunc->FunctionReference.IsSelfContext())
		{
			return FString::Printf(TEXT("CallFunction:%s.%s"), *FuncClass->GetName(), *EncodedFuncName);
		}
		return FString::Printf(TEXT("CallFunction:%s"), *EncodedFuncName);
	}

	if (NodeClass == UK2Node_VariableGet::StaticClass())
	{
		UK2Node_VariableGet* VarGet = CastChecked<UK2Node_VariableGet>(Node);
		FName VarName = VarGet->VariableReference.GetMemberName();
		return FString::Printf(TEXT("VariableGet:%s"), *NodeCodeUtils::EncodeSpaces(VarName.ToString()));
	}

	if (NodeClass == UK2Node_VariableSet::StaticClass())
	{
		UK2Node_VariableSet* VarSet = CastChecked<UK2Node_VariableSet>(Node);
		FName VarName = VarSet->VariableReference.GetMemberName();
		return FString::Printf(TEXT("VariableSet:%s"), *NodeCodeUtils::EncodeSpaces(VarName.ToString()));
	}

	if (NodeClass == UK2Node_CustomEvent::StaticClass())
	{
		UK2Node_CustomEvent* CustomEvent = CastChecked<UK2Node_CustomEvent>(Node);
		return FString::Printf(TEXT("CustomEvent:%s"), *NodeCodeUtils::EncodeSpaces(CustomEvent->CustomFunctionName.ToString()));
	}

	if (NodeClass == UK2Node_Event::StaticClass())
	{
		UK2Node_Event* Event = CastChecked<UK2Node_Event>(Node);
		FName EventName = Event->EventReference.GetMemberName();
		if (!EventName.IsNone())
		{
			return FString::Printf(TEXT("Event:%s"), *NodeCodeUtils::EncodeSpaces(EventName.ToString()));
		}
		if (!Event->CustomFunctionName.IsNone())
		{
			return FString::Printf(TEXT("Event:%s"), *NodeCodeUtils::EncodeSpaces(Event->CustomFunctionName.ToString()));
		}
	}

	if (Cast<UK2Node_FunctionEntry>(Node))
	{
		return TEXT("FunctionEntry");
	}

	if (Cast<UK2Node_FunctionResult>(Node))
	{
		return TEXT("FunctionResult");
	}

	return NodeClass->GetName();
}

const TSet<FName>& GetBaseClassSkipProperties()
{
	static TSet<FName> SkipSet;
	if (SkipSet.Num() == 0)
	{
		SkipSet.Add(TEXT("NodePosX"));
		SkipSet.Add(TEXT("NodePosY"));
		SkipSet.Add(TEXT("NodeWidth"));
		SkipSet.Add(TEXT("NodeHeight"));
		SkipSet.Add(TEXT("NodeGuid"));
		SkipSet.Add(TEXT("NodeComment"));
		SkipSet.Add(TEXT("ErrorType"));
		SkipSet.Add(TEXT("ErrorMsg"));
		SkipSet.Add(TEXT("bHasCompilerMessage"));
		SkipSet.Add(TEXT("bCommentBubblePinned"));
		SkipSet.Add(TEXT("bCommentBubbleVisible"));
		SkipSet.Add(TEXT("bCommentBubbleMakeVisible"));
		SkipSet.Add(TEXT("bCanRenameNode"));
		SkipSet.Add(TEXT("bCanResizeNode"));
		SkipSet.Add(TEXT("bDisplayAsDisabled"));
		SkipSet.Add(TEXT("bUserSetEnabledState"));
		SkipSet.Add(TEXT("bIsNodeEnabled_DEPRECATED"));
		SkipSet.Add(TEXT("bIsIntermediateNode"));
		SkipSet.Add(TEXT("AdvancedPinDisplay"));
		SkipSet.Add(TEXT("EnabledState"));
		SkipSet.Add(TEXT("DeprecatedPins"));
		SkipSet.Add(TEXT("NodeUpgradeMessage"));
		// CallFunction internals derived from the function reference
		SkipSet.Add(TEXT("bDefaultsToPureFunc"));
		SkipSet.Add(TEXT("bWantsEnumToExecExpansion"));
		SkipSet.Add(TEXT("bIsPureFunc"));
		SkipSet.Add(TEXT("bIsConstFunc"));
		SkipSet.Add(TEXT("bIsInterfaceCall"));
		SkipSet.Add(TEXT("bIsFinalFunction"));
		SkipSet.Add(TEXT("bIsBeadFunction"));
		SkipSet.Add(TEXT("NodePurityOverride"));
		// Deprecated fields
		SkipSet.Add(TEXT("CallFunctionName_DEPRECATED"));
		SkipSet.Add(TEXT("CallFunctionClass_DEPRECATED"));
		SkipSet.Add(TEXT("EventSignatureName_DEPRECATED"));
		SkipSet.Add(TEXT("EventSignatureClass_DEPRECATED"));
		SkipSet.Add(TEXT("VariableSourceClass_DEPRECATED"));
		SkipSet.Add(TEXT("VariableName_DEPRECATED"));
	}
	return SkipSet;
}

void FBlueprintGraphSerializer::SerializeNodeProperties(
	UEdGraphNode* Node,
	TMap<FString, FString>& OutProperties)
{
	UClass* NodeClass = Node->GetClass();
	UObject* CDO = NodeClass->GetDefaultObject();
	const TSet<FName>& SkipSet = GetBaseClassSkipProperties();

	for (TFieldIterator<FProperty> PropIt(NodeClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;

		if (FNodeCodePropertyUtils::ShouldSkipProperty(Prop))
		{
			continue;
		}

		if (SkipSet.Contains(Prop->GetFName()))
		{
			continue;
		}

		const void* InstanceValue = Prop->ContainerPtrToValuePtr<void>(Node);
		const void* CDOValue = Prop->ContainerPtrToValuePtr<void>(CDO);

		if (Prop->Identical(InstanceValue, CDOValue, PPF_None))
		{
			continue;
		}

		FString ValueStr = FNodeCodePropertyUtils::FormatPropertyValue(Prop, InstanceValue, Node);
		OutProperties.Add(Prop->GetName(), MoveTemp(ValueStr));
	}
}

void FBlueprintGraphSerializer::SerializePinDefaults(
	UEdGraphNode* Node,
	TMap<FString, FString>& OutProperties)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input)
		{
			continue;
		}

		if (Pin->bHidden || Pin->LinkedTo.Num() > 0)
		{
			continue;
		}

		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}

		FString DefaultValue;
		if (Pin->DefaultObject)
		{
			DefaultValue = Pin->DefaultObject->GetPathName();
		}
		else if (!Pin->DefaultValue.IsEmpty())
		{
			DefaultValue = Pin->DefaultValue;
		}
		else if (!Pin->DefaultTextValue.IsEmpty())
		{
			DefaultValue = Pin->DefaultTextValue.ToString();
		}
		else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
		{
			continue;
		}
		else
		{
			continue;
		}

		FString EncodedPinName = NodeCodeUtils::EncodeSpaces(Pin->PinName.ToString());
		FString PinKey = FString::Printf(TEXT("pin.%s"), *EncodedPinName);
		OutProperties.Add(PinKey, DefaultValue);
	}
}
