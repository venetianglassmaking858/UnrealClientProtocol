// MIT License - Copyright (c) 2025 Italink

#include "Blueprint/BlueprintGraphDiffer.h"
#include "Blueprint/BlueprintGraphSerializer.h"
#include "NodeCode/NodeCodeTextFormat.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
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
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "ScopedTransaction.h"

DEFINE_LOG_CATEGORY_STATIC(LogUCPBlueprint, Log, All);

struct FBPPinPair
{
	UEdGraphPin* FromPin = nullptr;
	UEdGraphPin* ToPin = nullptr;

	bool operator==(const FBPPinPair& Other) const { return FromPin == Other.FromPin && ToPin == Other.ToPin; }
	friend uint32 GetTypeHash(const FBPPinPair& P) { return HashCombine(::GetTypeHash(P.FromPin), ::GetTypeHash(P.ToPin)); }
};

static FName DecodeFName(const FString& Encoded)
{
	return FName(*Encoded.Replace(TEXT("_"), TEXT(" ")));
}

static UFunction* FindFunctionFuzzy(UClass* Class, const FString& EncodedName)
{
	if (!Class)
	{
		return nullptr;
	}
	UFunction* Func = Class->FindFunctionByName(FName(*EncodedName));
	if (Func)
	{
		return Func;
	}
	return Class->FindFunctionByName(DecodeFName(EncodedName));
}

FNodeCodeDiffResult FBlueprintGraphDiffer::Apply(UBlueprint* Blueprint, const FString& ScopeName, const FString& GraphText)
{
	if (!Blueprint)
	{
		UE_LOG(LogUCPBlueprint, Error, TEXT("WriteGraph: Blueprint is null"));
		return {};
	}

	UEdGraph* Graph = FBlueprintGraphSerializer::FindGraphByScope(Blueprint, ScopeName);
	if (!Graph)
	{
		UE_LOG(LogUCPBlueprint, Error, TEXT("WriteGraph: Graph not found for scope: %s"), *ScopeName);
		return {};
	}

	FNodeCodeGraphIR NewIR = FNodeCodeTextFormat::ParseText(GraphText);
	return DiffAndApply(Blueprint, Graph, ScopeName, NewIR);
}

void FBlueprintGraphDiffer::MatchNodes(
	const FNodeCodeGraphIR& OldIR,
	const FNodeCodeGraphIR& NewIR,
	TMap<int32, int32>& OutNewToOld)
{
	TMap<FGuid, int32> OldGuidMap;
	for (int32 i = 0; i < OldIR.Nodes.Num(); ++i)
	{
		if (OldIR.Nodes[i].Guid.IsValid())
		{
			OldGuidMap.Add(OldIR.Nodes[i].Guid, i);
		}
	}

	TSet<int32> MatchedOld;

	// Pass 1: Match by Guid
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		if (NewNode.Guid.IsValid())
		{
			if (int32* OldIdx = OldGuidMap.Find(NewNode.Guid))
			{
				if (!MatchedOld.Contains(*OldIdx))
				{
					OutNewToOld.Add(NewIdx, *OldIdx);
					MatchedOld.Add(*OldIdx);
				}
			}
		}
	}

	// Pass 2: Match by ClassName (which encodes function/variable/event identity)
	TMultiMap<FString, int32> UnmatchedOldByClass;
	for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
	{
		if (!MatchedOld.Contains(OldIdx))
		{
			UnmatchedOldByClass.Add(OldIR.Nodes[OldIdx].ClassName, OldIdx);
		}
	}

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		if (OutNewToOld.Contains(NewIdx))
		{
			continue;
		}

		const FString& ClassName = NewIR.Nodes[NewIdx].ClassName;
		for (auto It = UnmatchedOldByClass.CreateKeyIterator(ClassName); It; ++It)
		{
			int32 OldIdx = It.Value();
			OutNewToOld.Add(NewIdx, OldIdx);
			MatchedOld.Add(OldIdx);
			It.RemoveCurrent();
			break;
		}
	}
}

UEdGraphNode* FBlueprintGraphDiffer::CreateNodeFromIR(
	UEdGraph* Graph,
	UBlueprint* Blueprint,
	const FNodeCodeNodeIR& NodeIR)
{
	const FString& ClassName = NodeIR.ClassName;

	// CallFunction:<ClassName>.<FuncName> or CallFunction:<FuncName>
	if (ClassName.StartsWith(TEXT("CallFunction:")))
	{
		FString FuncSpec = ClassName.Mid(14); // len("CallFunction:")
		FString OwnerClassHint, FuncNameEncoded;
		bool bHasOwner = FuncSpec.Split(TEXT("."), &OwnerClassHint, &FuncNameEncoded);
		if (!bHasOwner)
		{
			FuncNameEncoded = FuncSpec;
		}

		// Resolve owner class: prefer Target property (full path), fallback to ClassName hint (short name)
		UClass* OwnerClass = nullptr;
		if (const FString* TargetPath = NodeIR.Properties.Find(TEXT("Target")))
		{
			OwnerClass = UClass::TryFindTypeSlow<UClass>(*TargetPath);
		}
		if (!OwnerClass && bHasOwner)
		{
			OwnerClass = UClass::TryFindTypeSlow<UClass>(OwnerClassHint);
		}

		// Resolve function name: prefer Function property, fallback to ClassName-encoded name
		FString FuncNameStr = FuncNameEncoded;
		if (const FString* FuncProp = NodeIR.Properties.Find(TEXT("Function")))
		{
			FuncNameStr = *FuncProp;
		}

		if (OwnerClass || !bHasOwner)
		{
			UClass* SearchClass = OwnerClass ? OwnerClass : Blueprint->GeneratedClass.Get();
			UFunction* Func = FindFunctionFuzzy(SearchClass, FuncNameStr);
			if (Func)
			{
				FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
				UK2Node_CallFunction* Node = Creator.CreateNode();
				Node->SetFromFunction(Func);
				Creator.Finalize();
				return Node;
			}
		}

		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode();
		FName ResolvedName = DecodeFName(FuncNameStr);
		if (OwnerClass)
		{
			Node->FunctionReference.SetExternalMember(ResolvedName, OwnerClass);
		}
		else
		{
			Node->FunctionReference.SetSelfMember(ResolvedName);
		}
		Node->AllocateDefaultPins();
		Creator.Finalize();
		return Node;
	}

	// VariableGet:<VarName>
	if (ClassName.StartsWith(TEXT("VariableGet:")))
	{
		FString VarName = ClassName.Mid(12);
		if (const FString* VarProp = NodeIR.Properties.Find(TEXT("Variable")))
		{
			VarName = *VarProp;
		}
		FName ResolvedName = DecodeFName(VarName);
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode();
		if (const FString* TargetPath = NodeIR.Properties.Find(TEXT("Target")))
		{
			UClass* TargetClass = UClass::TryFindTypeSlow<UClass>(*TargetPath);
			if (TargetClass)
			{
				Node->VariableReference.SetExternalMember(ResolvedName, TargetClass);
			}
			else
			{
				Node->VariableReference.SetSelfMember(ResolvedName);
			}
		}
		else
		{
			Node->VariableReference.SetSelfMember(ResolvedName);
		}
		Node->AllocateDefaultPins();
		Creator.Finalize();
		return Node;
	}

	// VariableSet:<VarName>
	if (ClassName.StartsWith(TEXT("VariableSet:")))
	{
		FString VarName = ClassName.Mid(12);
		if (const FString* VarProp = NodeIR.Properties.Find(TEXT("Variable")))
		{
			VarName = *VarProp;
		}
		FName ResolvedName = DecodeFName(VarName);
		FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
		UK2Node_VariableSet* Node = Creator.CreateNode();
		if (const FString* TargetPath = NodeIR.Properties.Find(TEXT("Target")))
		{
			UClass* TargetClass = UClass::TryFindTypeSlow<UClass>(*TargetPath);
			if (TargetClass)
			{
				Node->VariableReference.SetExternalMember(ResolvedName, TargetClass);
			}
			else
			{
				Node->VariableReference.SetSelfMember(ResolvedName);
			}
		}
		else
		{
			Node->VariableReference.SetSelfMember(ResolvedName);
		}
		Node->AllocateDefaultPins();
		Creator.Finalize();
		return Node;
	}

	// CustomEvent:<EventName>
	if (ClassName.StartsWith(TEXT("CustomEvent:")))
	{
		FString EventName = ClassName.Mid(12);
		if (const FString* EvProp = NodeIR.Properties.Find(TEXT("EventName")))
		{
			EventName = *EvProp;
		}
		FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
		UK2Node_CustomEvent* Node = Creator.CreateNode();
		Node->CustomFunctionName = DecodeFName(EventName);
		Node->AllocateDefaultPins();
		Creator.Finalize();
		return Node;
	}

	// Event:<EventName>
	if (ClassName.StartsWith(TEXT("Event:")))
	{
		FString EventName = ClassName.Mid(6);
		if (const FString* EvProp = NodeIR.Properties.Find(TEXT("EventName")))
		{
			EventName = *EvProp;
		}

		// Resolve target class for the event
		UClass* EventClass = Blueprint->GeneratedClass;
		if (const FString* TargetPath = NodeIR.Properties.Find(TEXT("Target")))
		{
			UClass* TargetClass = UClass::TryFindTypeSlow<UClass>(*TargetPath);
			if (TargetClass)
			{
				EventClass = TargetClass;
			}
		}

		if (EventClass)
		{
			UFunction* EventFunc = FindFunctionFuzzy(EventClass, EventName);
			if (EventFunc)
			{
				FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
				UK2Node_Event* Node = Creator.CreateNode();
				Node->EventReference.SetFromField<UFunction>(EventFunc, false);
				Node->bOverrideFunction = true;
				Node->AllocateDefaultPins();
				Creator.Finalize();
				return Node;
			}
		}
		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		UK2Node_Event* Node = Creator.CreateNode();
		Node->EventReference.SetSelfMember(DecodeFName(EventName));
		Node->AllocateDefaultPins();
		Creator.Finalize();
		return Node;
	}

	// FunctionEntry / FunctionResult — these should already exist, not created
	if (ClassName == TEXT("FunctionEntry") || ClassName == TEXT("FunctionResult"))
	{
		UE_LOG(LogUCPBlueprint, Warning, TEXT("WriteGraph: Cannot create %s nodes — they are auto-generated"), *ClassName);
		return nullptr;
	}

	// Generic UEdGraphNode subclass by class name
	UClass* NodeClass = UClass::TryFindTypeSlow<UClass>(ClassName, EFindFirstObjectOptions::ExactClass);
	if (!NodeClass)
	{
		NodeClass = UClass::TryFindTypeSlow<UClass>(FString::Printf(TEXT("K2Node_%s"), *ClassName), EFindFirstObjectOptions::ExactClass);
	}
	if (NodeClass && NodeClass->IsChildOf(UEdGraphNode::StaticClass()))
	{
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, false, false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		return Node;
	}

	UE_LOG(LogUCPBlueprint, Error, TEXT("WriteGraph: Unknown node class: %s"), *ClassName);
	return nullptr;
}

void FBlueprintGraphDiffer::ApplyPropertyChanges(
	UEdGraphNode* Node,
	const TMap<FString, FString>& NewProperties,
	TArray<FString>& OutChanges)
{
	UClass* NodeClass = Node->GetClass();

	for (const auto& Pair : NewProperties)
	{
		if (Pair.Key.StartsWith(TEXT("pin.")))
		{
			continue;
		}

		FProperty* Prop = NodeClass->FindPropertyByName(FName(*Pair.Key));
		if (!Prop)
		{
			OutChanges.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);

		FString OldValue;
		Prop->ExportTextItem_Direct(OldValue, ValuePtr, nullptr, Node, PPF_None, nullptr);

		FString ImportValue = Pair.Value;
		if (ImportValue.StartsWith(TEXT("\"")) && ImportValue.EndsWith(TEXT("\"")))
		{
			ImportValue = ImportValue.Mid(1, ImportValue.Len() - 2);
			ImportValue = ImportValue.ReplaceEscapedCharWithChar();
		}

		const TCHAR* Buffer = *ImportValue;
		const TCHAR* Result = Prop->ImportText_Direct(Buffer, ValuePtr, Node, PPF_None, GWarn);

		if (Result)
		{
			FString NewValue;
			Prop->ExportTextItem_Direct(NewValue, ValuePtr, nullptr, Node, PPF_None, nullptr);
			if (OldValue != NewValue)
			{
				OutChanges.Add(FString::Printf(TEXT("%s: %s -> %s"), *Pair.Key, *OldValue, *NewValue));
			}
		}
		else
		{
			OutChanges.Add(FString::Printf(TEXT("Failed to import %s = %s"), *Pair.Key, *Pair.Value));
		}
	}
}

void FBlueprintGraphDiffer::ApplyPinDefaults(
	UEdGraphNode* Node,
	const TMap<FString, FString>& Properties,
	TArray<FString>& OutChanges)
{
	for (const auto& Pair : Properties)
	{
		if (!Pair.Key.StartsWith(TEXT("pin.")))
		{
			continue;
		}

		FString EncodedPinName = Pair.Key.Mid(4);
		UEdGraphPin* Pin = FindPinByName(Node, EncodedPinName, EGPD_Input);
		if (!Pin)
		{
			OutChanges.Add(FString::Printf(TEXT("Pin not found: %s"), *EncodedPinName));
			continue;
		}

		if (Pin->DefaultValue != Pair.Value)
		{
			Pin->DefaultValue = Pair.Value;
			OutChanges.Add(FString::Printf(TEXT("pin.%s: %s"), *EncodedPinName, *Pair.Value));
		}
	}
}

UEdGraphPin* FBlueprintGraphDiffer::FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == Direction && NodeCodeUtils::MatchName(PinName, Pin->PinName.ToString()))
		{
			return Pin;
		}
	}
	return nullptr;
}

FNodeCodeDiffResult FBlueprintGraphDiffer::DiffAndApply(
	UBlueprint* Blueprint,
	UEdGraph* Graph,
	const FString& ScopeName,
	const FNodeCodeGraphIR& NewIR)
{
	FNodeCodeDiffResult Result;

	FNodeCodeGraphIR OldIR = FBlueprintGraphSerializer::BuildIR(Blueprint, ScopeName);

	TMap<int32, int32> NewToOld;
	MatchNodes(OldIR, NewIR, NewToOld);

	TSet<int32> MatchedOldIndices;
	for (auto& Pair : NewToOld)
	{
		MatchedOldIndices.Add(Pair.Value);
	}

	FScopedTransaction Transaction(NSLOCTEXT("UCPBlueprintGraph", "WriteGraph", "UCP: Write Blueprint Graph"));

	Graph->Modify();

	// Phase 1: Delete removed nodes
	for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
	{
		if (MatchedOldIndices.Contains(OldIdx))
		{
			continue;
		}

		const FNodeCodeNodeIR& OldNode = OldIR.Nodes[OldIdx];
		UEdGraphNode* OldGraphNode = Cast<UEdGraphNode>(OldNode.SourceObject);
		if (OldGraphNode)
		{
			// Don't delete structural nodes (FunctionEntry, FunctionResult)
			if (Cast<UK2Node_FunctionEntry>(OldGraphNode) || Cast<UK2Node_FunctionResult>(OldGraphNode))
			{
				continue;
			}
			Graph->RemoveNode(OldGraphNode);
			Result.NodesRemoved.Add(FString::Printf(TEXT("N%d %s"), OldNode.Index, *OldNode.ClassName));
		}
	}

	// Phase 2: Create new nodes and build index map
	TMap<int32, UEdGraphNode*> NewIndexToNode;

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];

		if (int32* OldIdx = NewToOld.Find(NewIdx))
		{
			NewIndexToNode.Add(NewNode.Index, Cast<UEdGraphNode>(OldIR.Nodes[*OldIdx].SourceObject));
		}
		else
		{
			UEdGraphNode* CreatedNode = CreateNodeFromIR(Graph, Blueprint, NewNode);
			if (CreatedNode)
			{
				NewIndexToNode.Add(NewNode.Index, CreatedNode);
				Result.NodesAdded.Add(FString::Printf(TEXT("N%d %s"), NewNode.Index, *NewNode.ClassName));
			}
		}
	}

	// Phase 3: Apply property changes on matched nodes
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		int32* OldIdx = NewToOld.Find(NewIdx);
		if (!OldIdx)
		{
			continue;
		}

		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		const FNodeCodeNodeIR& OldNode = OldIR.Nodes[*OldIdx];
		UEdGraphNode* GraphNode = Cast<UEdGraphNode>(OldNode.SourceObject);

		if (!GraphNode)
		{
			continue;
		}

		bool bPropsChanged = (NewNode.Properties.Num() != OldNode.Properties.Num());
		if (!bPropsChanged)
		{
			for (const auto& Pair : NewNode.Properties)
			{
				const FString* OldVal = OldNode.Properties.Find(Pair.Key);
				if (!OldVal || *OldVal != Pair.Value)
				{
					bPropsChanged = true;
					break;
				}
			}
		}

		if (bPropsChanged)
		{
			TArray<FString> Changes;
			ApplyPropertyChanges(GraphNode, NewNode.Properties, Changes);
			ApplyPinDefaults(GraphNode, NewNode.Properties, Changes);

			if (Changes.Num() > 0)
			{
				Result.NodesModified.Add(FString::Printf(TEXT("N%d: %s"),
					NewNode.Index, *FString::Join(Changes, TEXT("; "))));
			}
		}
	}

	// Phase 4: Apply pin defaults on newly created nodes
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		if (NewToOld.Contains(NewIdx))
		{
			continue;
		}

		const FNodeCodeNodeIR& NewNode = NewIR.Nodes[NewIdx];
		UEdGraphNode** NodePtr = NewIndexToNode.Find(NewNode.Index);
		if (NodePtr && *NodePtr)
		{
			TArray<FString> Changes;
			ApplyPinDefaults(*NodePtr, NewNode.Properties, Changes);
		}
	}

	// Phase 5+6: Incremental connection diff
	// Resolve NewIR links to pin pairs, diff against live graph, only touch changes.

	// Build desired link set from NewIR
	TSet<FBPPinPair> DesiredLinks;
	TArray<FBPPinPair> LinksToCreate;

	for (const FNodeCodeLinkIR& Link : NewIR.Links)
	{
		UEdGraphNode** FromNodePtr = NewIndexToNode.Find(Link.FromNodeIndex);
		UEdGraphNode** ToNodePtr = NewIndexToNode.Find(Link.ToNodeIndex);
		if (!FromNodePtr || !*FromNodePtr || !ToNodePtr || !*ToNodePtr)
		{
			continue;
		}

		UEdGraphPin* FromPin = FindPinByName(*FromNodePtr, Link.FromOutputName, EGPD_Output);
		UEdGraphPin* ToPin = FindPinByName(*ToNodePtr, Link.ToInputName, EGPD_Input);
		if (!FromPin || !ToPin)
		{
			if (!FromPin)
			{
				UE_LOG(LogUCPBlueprint, Warning, TEXT("WriteGraph: Output pin '%s' not found on N%d"), *Link.FromOutputName, Link.FromNodeIndex);
			}
			if (!ToPin)
			{
				UE_LOG(LogUCPBlueprint, Warning, TEXT("WriteGraph: Input pin '%s' not found on N%d"), *Link.ToInputName, Link.ToNodeIndex);
			}
			continue;
		}

		FBPPinPair Pair{FromPin, ToPin};
		DesiredLinks.Add(Pair);

		if (!FromPin->LinkedTo.Contains(ToPin))
		{
			LinksToCreate.Add(Pair);
		}
	}

	// Build scope node set for filtering
	TSet<UEdGraphNode*> ScopeNodeSet;
	for (auto& Pair : NewIndexToNode)
	{
		if (Pair.Value)
		{
			ScopeNodeSet.Add(Pair.Value);
		}
	}

	// Remove links that exist in live graph but not in desired set (only between in-scope nodes)
	for (auto& Pair : NewIndexToNode)
	{
		UEdGraphNode* GraphNode = Pair.Value;
		if (!GraphNode)
		{
			continue;
		}

		for (UEdGraphPin* Pin : GraphNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output)
			{
				continue;
			}

			TArray<UEdGraphPin*> LinksToBreak;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !ScopeNodeSet.Contains(LinkedPin->GetOwningNode()))
				{
					continue;
				}
				FBPPinPair LivePair{Pin, LinkedPin};
				if (!DesiredLinks.Contains(LivePair))
				{
					LinksToBreak.Add(LinkedPin);
				}
			}
			for (UEdGraphPin* LinkedPin : LinksToBreak)
			{
				Pin->BreakLinkTo(LinkedPin);
				Result.LinksRemoved.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
					*Pin->GetOwningNode()->GetName(), *Pin->PinName.ToString(),
					*LinkedPin->GetOwningNode()->GetName(), *LinkedPin->PinName.ToString()));
			}
		}
	}

	// Create new links
	const UEdGraphSchema* Schema = Graph->GetSchema();

	for (const FBPPinPair& Pair : LinksToCreate)
	{
		if (Schema)
		{
			Schema->TryCreateConnection(Pair.FromPin, Pair.ToPin);
		}
		else
		{
			Pair.FromPin->MakeLinkTo(Pair.ToPin);
		}

		Result.LinksAdded.Add(FString::Printf(TEXT("%s.%s -> %s.%s"),
			*Pair.FromPin->GetOwningNode()->GetName(), *Pair.FromPin->PinName.ToString(),
			*Pair.ToPin->GetOwningNode()->GetName(), *Pair.ToPin->PinName.ToString()));
	}

	// Phase 7: Mark blueprint as modified and recompile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	return Result;
}
