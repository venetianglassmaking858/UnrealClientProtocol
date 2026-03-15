// MIT License - Copyright (c) 2025 Italink

#include "MaterialGraphSerializer.h"
#include "MaterialExpressionClassCache.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionComposite.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSwitch.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "UObject/UnrealType.h"

static UMaterialExpression* TraceReroute(FExpressionInput* Input, int32& OutOutputIndex)
{
	if (!Input || !Input->Expression)
	{
		return nullptr;
	}

	if (UMaterialExpressionRerouteBase* Reroute = Cast<UMaterialExpressionRerouteBase>(Input->Expression))
	{
		return Reroute->TraceInputsToRealExpression(OutOutputIndex);
	}

	OutOutputIndex = Input->OutputIndex;
	return Input->Expression;
}

static FString FormatPropertyValue(FProperty* Prop, const void* ValuePtr, UObject* Owner)
{
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		if (!Obj)
		{
			return TEXT("None");
		}
		if (Obj->IsAsset())
		{
			return FString::Printf(TEXT("\"%s\""), *Obj->GetPathName());
		}
		FString ExportStr;
		Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
		return ExportStr;
	}

	if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		FString ExportStr;
		Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
		return ExportStr;
	}

	if (CastField<FStrProperty>(Prop))
	{
		const FString& Str = *static_cast<const FString*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Str.ReplaceCharWithEscapedChar());
	}

	if (CastField<FNameProperty>(Prop))
	{
		const FName& Name = *static_cast<const FName*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Name.ToString());
	}

	if (CastField<FTextProperty>(Prop))
	{
		const FText& Text = *static_cast<const FText*>(ValuePtr);
		return FString::Printf(TEXT("\"%s\""), *Text.ToString());
	}

	FString ExportStr;
	Prop->ExportTextItem_Direct(ExportStr, ValuePtr, nullptr, Owner, PPF_None, nullptr);
	return ExportStr;
}

// -------------------------------------------------------------------

FString FMaterialGraphSerializer::Serialize(UMaterial* Material, const FString& ScopeName)
{
	FMGGraphIR IR = BuildIR(Material, ScopeName);
	return IRToText(IR);
}

FString FMaterialGraphSerializer::Serialize(UMaterialFunction* MaterialFunction, const FString& ScopeName)
{
	FMGGraphIR IR = BuildIR(MaterialFunction, ScopeName);
	return IRToText(IR);
}

FMGGraphIR FMaterialGraphSerializer::BuildIR(UMaterial* Material, const FString& ScopeName)
{
	if (!Material)
	{
		return {};
	}

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
	return BuildIRFromExpressions(Expressions, Material, nullptr, ScopeName);
}

FMGGraphIR FMaterialGraphSerializer::BuildIR(UMaterialFunction* MaterialFunction, const FString& ScopeName)
{
	if (!MaterialFunction)
	{
		return {};
	}

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = MaterialFunction->GetExpressions();
	return BuildIRFromExpressions(Expressions, nullptr, MaterialFunction, ScopeName);
}

FMGGraphIR FMaterialGraphSerializer::BuildIRFromExpressions(
	TConstArrayView<TObjectPtr<UMaterialExpression>> AllExpressions,
	UMaterial* Material,
	UMaterialFunction* MaterialFunction,
	const FString& ScopeName)
{
	FMaterialExpressionClassCache::Get().Build();

	FMGGraphIR IR;
	IR.ScopeName = ScopeName;

	TSet<UMaterialExpression*> ReachableSet;
	TSet<UMaterialExpression*> Visited;

	bool bScopeIsComposite = false;
	UMaterialExpressionComposite* TargetComposite = nullptr;

	if (Material)
	{
		if (ScopeName.IsEmpty())
		{
			for (int32 i = 0; i < MP_MAX; ++i)
			{
				FExpressionInput* Input = Material->GetExpressionInputForProperty(static_cast<EMaterialProperty>(i));
				if (Input && Input->Expression)
				{
					int32 OutIdx;
					UMaterialExpression* RealExpr = TraceReroute(Input, OutIdx);
					if (RealExpr)
					{
						CollectReachableNodes(RealExpr, ReachableSet, Visited);
					}
				}
			}

#if WITH_EDITORONLY_DATA
			TArray<UMaterialExpressionCustomOutput*> CustomOutputs;
			Material->GetAllCustomOutputExpressions(CustomOutputs);
			for (UMaterialExpressionCustomOutput* CustomOut : CustomOutputs)
			{
				CollectReachableNodes(CustomOut, ReachableSet, Visited);
			}
#endif
		}
		else
		{
			for (int32 i = 0; i < MP_MAX; ++i)
			{
				EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
				const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
				if (AttrName == ScopeName)
				{
					FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
					if (Input && Input->Expression)
					{
						int32 OutIdx;
						UMaterialExpression* RealExpr = TraceReroute(Input, OutIdx);
						if (RealExpr)
						{
							CollectReachableNodes(RealExpr, ReachableSet, Visited);
						}
					}
					break;
				}
			}

			if (ReachableSet.Num() == 0)
			{
				for (const TObjectPtr<UMaterialExpression>& Expr : AllExpressions)
				{
					UMaterialExpressionComposite* Composite = Cast<UMaterialExpressionComposite>(Expr.Get());
					if (Composite && Composite->SubgraphName == ScopeName)
					{
						TargetComposite = Composite;
						bScopeIsComposite = true;
						break;
					}
				}

				if (TargetComposite)
				{
					for (const TObjectPtr<UMaterialExpression>& Expr : AllExpressions)
					{
						if (Expr && Expr->SubgraphExpression == TargetComposite)
						{
							ReachableSet.Add(Expr.Get());
						}
					}
				}
			}
		}
	}
	else if (MaterialFunction)
	{
		for (const TObjectPtr<UMaterialExpression>& Expr : AllExpressions)
		{
			if (Expr && !Cast<UMaterialExpressionComment>(Expr.Get()))
			{
				ReachableSet.Add(Expr.Get());
			}
		}
	}

	TMap<UMaterialExpression*, int32> ExprToNodeIndex;
	int32 NodeCounter = 0;

	for (const TObjectPtr<UMaterialExpression>& Expr : AllExpressions)
	{
		if (!Expr || !ReachableSet.Contains(Expr.Get()))
		{
			continue;
		}
		if (Cast<UMaterialExpressionComment>(Expr.Get()))
		{
			continue;
		}
		if (!bScopeIsComposite && Expr->SubgraphExpression != nullptr)
		{
			continue;
		}
		if (Cast<UMaterialExpressionRerouteBase>(Expr.Get()))
		{
			continue;
		}

		FMGNodeIR Node;
		Node.Index = NodeCounter;
		Node.Expression = Expr.Get();
		Node.Guid = Expr->MaterialExpressionGuid;
		Node.ClassName = FMaterialExpressionClassCache::Get().GetSerializableName(Expr->GetClass());

		SerializeNodeProperties(Expr.Get(), Node.Properties);

		ExprToNodeIndex.Add(Expr.Get(), NodeCounter);
		IR.Nodes.Add(MoveTemp(Node));
		NodeCounter++;
	}

	if (Material && !bScopeIsComposite)
	{
		auto AddMaterialOutputLinks = [&](EMaterialProperty Prop, const FString& PinName)
		{
			FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
			if (!Input || !Input->Expression)
			{
				return;
			}

			int32 OutIdx;
			UMaterialExpression* RealExpr = TraceReroute(Input, OutIdx);
			if (!RealExpr)
			{
				return;
			}

			int32* FromIdx = ExprToNodeIndex.Find(RealExpr);
			if (!FromIdx)
			{
				return;
			}

			FMGLinkIR Link;
			Link.FromNodeIndex = *FromIdx;
			Link.FromOutputName = GetOutputPinName(RealExpr, OutIdx);
			Link.ToNodeIndex = -1;
			Link.ToInputName = PinName;
			Link.bToMaterialOutput = true;
			IR.Links.Add(MoveTemp(Link));
		};

		if (ScopeName.IsEmpty())
		{
			for (int32 i = 0; i < MP_MAX; ++i)
			{
				EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
				FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
				if (Input && Input->Expression)
				{
					const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
					AddMaterialOutputLinks(Prop, AttrName);
				}
			}
		}
		else if (!bScopeIsComposite)
		{
			for (int32 i = 0; i < MP_MAX; ++i)
			{
				EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
				const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
				if (AttrName == ScopeName)
				{
					AddMaterialOutputLinks(Prop, AttrName);
					break;
				}
			}
		}

#if WITH_EDITORONLY_DATA
		TArray<UMaterialExpressionCustomOutput*> CustomOutputs;
		Material->GetAllCustomOutputExpressions(CustomOutputs);
		for (UMaterialExpressionCustomOutput* CustomOut : CustomOutputs)
		{
			int32* ToIdx = ExprToNodeIndex.Find(CustomOut);
			if (!ToIdx)
			{
				continue;
			}

			for (FExpressionInputIterator It(CustomOut); It; ++It)
			{
				if (!It->Expression)
				{
					continue;
				}

				int32 OutIdx;
				UMaterialExpression* RealExpr = TraceReroute(It.Input, OutIdx);
				if (!RealExpr)
				{
					continue;
				}

				int32* FromIdx = ExprToNodeIndex.Find(RealExpr);
				if (!FromIdx)
				{
					continue;
				}

				FMGLinkIR Link;
				Link.FromNodeIndex = *FromIdx;
				Link.FromOutputName = GetOutputPinName(RealExpr, OutIdx);
				Link.ToNodeIndex = *ToIdx;
				Link.ToInputName = ShortenInputPinName(CustomOut->GetInputName(It.Index).ToString());
				Link.bToMaterialOutput = false;
				IR.Links.Add(MoveTemp(Link));
			}
		}
#endif
	}

	for (const FMGNodeIR& Node : IR.Nodes)
	{
		UMaterialExpression* Expr = Node.Expression;
		if (Cast<UMaterialExpressionCustomOutput>(Expr))
		{
			continue;
		}

		for (FExpressionInputIterator It(Expr); It; ++It)
		{
			if (!It->Expression)
			{
				continue;
			}

			int32 OutIdx;
			UMaterialExpression* RealExpr = TraceReroute(It.Input, OutIdx);
			if (!RealExpr)
			{
				continue;
			}

			int32* FromIdx = ExprToNodeIndex.Find(RealExpr);
			if (!FromIdx)
			{
				continue;
			}

			FMGLinkIR Link;
			Link.FromNodeIndex = *FromIdx;
			Link.FromOutputName = GetOutputPinName(RealExpr, OutIdx);
			Link.ToNodeIndex = Node.Index;
			Link.ToInputName = ShortenInputPinName(Expr->GetInputName(It.Index).ToString());
			Link.bToMaterialOutput = false;
			IR.Links.Add(MoveTemp(Link));
		}
	}

	return IR;
}

void FMaterialGraphSerializer::CollectReachableNodes(
	UMaterialExpression* StartExpr,
	TSet<UMaterialExpression*>& OutReachable,
	TSet<UMaterialExpression*>& Visited)
{
	if (!StartExpr || Visited.Contains(StartExpr))
	{
		return;
	}

	Visited.Add(StartExpr);
	OutReachable.Add(StartExpr);

	for (FExpressionInputIterator It(StartExpr); It; ++It)
	{
		if (It->Expression)
		{
			if (UMaterialExpressionRerouteBase* Reroute = Cast<UMaterialExpressionRerouteBase>(It->Expression))
			{
				int32 OutIdx;
				UMaterialExpression* RealExpr = Reroute->TraceInputsToRealExpression(OutIdx);
				if (RealExpr)
				{
					CollectReachableNodes(RealExpr, OutReachable, Visited);
				}
			}
			else
			{
				CollectReachableNodes(It->Expression, OutReachable, Visited);
			}
		}
	}
}

void FMaterialGraphSerializer::SerializeNodeProperties(
	UMaterialExpression* Expression,
	TMap<FString, FString>& OutProperties)
{
	UClass* ExprClass = Expression->GetClass();
	UObject* CDO = ExprClass->GetDefaultObject();
	const TSet<FName>& SkipSet = GetBaseClassSkipProperties();

	if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expression))
	{
		if (CustomExpr->Inputs.Num() > 0)
		{
			bool bHasNamedInputs = false;
			for (const FCustomInput& CI : CustomExpr->Inputs)
			{
				if (!CI.InputName.IsNone())
				{
					bHasNamedInputs = true;
					break;
				}
			}

			if (bHasNamedInputs)
			{
				FString InputNames = TEXT("[");
				bool bFirst = true;
				for (const FCustomInput& CI : CustomExpr->Inputs)
				{
					if (!bFirst) InputNames += TEXT(",");
					InputNames += FString::Printf(TEXT("\"%s\""), *CI.InputName.ToString());
					bFirst = false;
				}
				InputNames += TEXT("]");
				OutProperties.Add(TEXT("InputNames"), MoveTemp(InputNames));
			}
		}
	}

	if (UMaterialExpressionSwitch* SwitchExpr = Cast<UMaterialExpressionSwitch>(Expression))
	{
		if (SwitchExpr->Inputs.Num() > 0)
		{
			FString SwitchNames = TEXT("[");
			bool bFirst = true;
			for (const FSwitchCustomInput& SI : SwitchExpr->Inputs)
			{
				if (!bFirst) SwitchNames += TEXT(",");
				SwitchNames += FString::Printf(TEXT("\"%s\""), *SI.InputName.ToString());
				bFirst = false;
			}
			SwitchNames += TEXT("]");
			OutProperties.Add(TEXT("SwitchInputNames"), MoveTemp(SwitchNames));
		}
	}

	if (UMaterialExpressionSetMaterialAttributes* SetAttrExpr = Cast<UMaterialExpressionSetMaterialAttributes>(Expression))
	{
		if (SetAttrExpr->AttributeSetTypes.Num() > 0)
		{
			FString Attributes = TEXT("[");
			bool bFirst = true;
			for (const FGuid& AttrGuid : SetAttrExpr->AttributeSetTypes)
			{
				if (!bFirst) Attributes += TEXT(",");
				const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(AttrGuid);
				Attributes += FString::Printf(TEXT("\"%s\""), *AttrName);
				bFirst = false;
			}
			Attributes += TEXT("]");
			OutProperties.Add(TEXT("Attributes"), MoveTemp(Attributes));
		}
	}

	for (TFieldIterator<FProperty> PropIt(ExprClass); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;

		if (ShouldSkipProperty(Prop))
		{
			continue;
		}

		if (SkipSet.Contains(Prop->GetFName()))
		{
			continue;
		}

		if (IsConnectionProperty(Prop))
		{
			continue;
		}

		if (IsEmbeddedConnectionArrayProperty(Prop))
		{
			continue;
		}

		const void* InstanceValue = Prop->ContainerPtrToValuePtr<void>(Expression);
		const void* CDOValue = Prop->ContainerPtrToValuePtr<void>(CDO);

		if (Prop->Identical(InstanceValue, CDOValue, PPF_None))
		{
			continue;
		}

		FString ValueStr = FormatPropertyValue(Prop, InstanceValue, Expression);
		OutProperties.Add(Prop->GetName(), MoveTemp(ValueStr));
	}
}

FString FMaterialGraphSerializer::GetOutputPinName(UMaterialExpression* Expression, int32 OutputIndex)
{
	TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();

	if (Outputs.Num() <= 1)
	{
		return FString();
	}

	if (OutputIndex >= 0 && OutputIndex < Outputs.Num())
	{
		const FExpressionOutput& Output = Outputs[OutputIndex];
		if (!Output.OutputName.IsNone())
		{
			return Output.OutputName.ToString();
		}

		if (Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA) return TEXT("R");
		if (!Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA) return TEXT("G");
		if (!Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA) return TEXT("B");
		if (!Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA) return TEXT("A");
		if (Output.MaskR && Output.MaskG && Output.MaskB && !Output.MaskA) return TEXT("RGB");
		if (Output.MaskR && Output.MaskG && Output.MaskB && Output.MaskA) return TEXT("RGBA");
	}

	return FString::FromInt(OutputIndex);
}

FString FMaterialGraphSerializer::ShortenInputPinName(const FString& InputName)
{
	return InputName;
}

bool FMaterialGraphSerializer::IsConnectionProperty(const FProperty* Prop)
{
	const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
	if (!StructProp || !StructProp->Struct)
	{
		return false;
	}

	const UStruct* Current = StructProp->Struct;
	while (Current)
	{
		if (Current->GetFName() == NAME_ExpressionInput)
		{
			return true;
		}
		Current = Current->GetSuperStruct();
	}

	return false;
}

bool FMaterialGraphSerializer::IsEmbeddedConnectionArrayProperty(const FProperty* Prop)
{
	const FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
	if (!ArrayProp)
	{
		return false;
	}

	const FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner);
	if (!InnerStruct || !InnerStruct->Struct)
	{
		return false;
	}

	for (TFieldIterator<FStructProperty> It(InnerStruct->Struct); It; ++It)
	{
		const UStruct* Current = It->Struct;
		while (Current)
		{
			if (Current->GetFName() == NAME_ExpressionInput)
			{
				return true;
			}
			Current = Current->GetSuperStruct();
		}
	}

	return false;
}

bool FMaterialGraphSerializer::ShouldSkipProperty(const FProperty* Prop)
{
	if (Prop->HasAnyPropertyFlags(
		CPF_Transient | CPF_DuplicateTransient | CPF_SkipSerialization | CPF_Deprecated))
	{
		return true;
	}

	return false;
}

const TSet<FName>& FMaterialGraphSerializer::GetBaseClassSkipProperties()
{
	static TSet<FName> SkipSet;
	if (SkipSet.Num() == 0)
	{
		SkipSet.Add(TEXT("MaterialExpressionEditorX"));
		SkipSet.Add(TEXT("MaterialExpressionEditorY"));
		SkipSet.Add(TEXT("MaterialExpressionGuid"));
		SkipSet.Add(TEXT("Material"));
		SkipSet.Add(TEXT("Function"));
		SkipSet.Add(TEXT("GraphNode"));
		SkipSet.Add(TEXT("bRealtimePreview"));
		SkipSet.Add(TEXT("bNeedToUpdatePreview"));
		SkipSet.Add(TEXT("bIsParameterExpression"));
		SkipSet.Add(TEXT("bShowOutputNameOnPin"));
		SkipSet.Add(TEXT("bShowMaskColorsOnPin"));
		SkipSet.Add(TEXT("bHidePreviewWindow"));
		SkipSet.Add(TEXT("bCollapsed"));
		SkipSet.Add(TEXT("bShaderInputData"));
		SkipSet.Add(TEXT("Outputs"));
		SkipSet.Add(TEXT("Desc"));
		SkipSet.Add(TEXT("SubgraphExpression"));
		SkipSet.Add(TEXT("AttributeSetTypes"));
		SkipSet.Add(TEXT("PreEditAttributeSetTypes"));
	}
	return SkipSet;
}

FString FMaterialGraphSerializer::IRToText(const FMGGraphIR& IR)
{
	FString Result;

	if (!IR.ScopeName.IsEmpty())
	{
		Result += FString::Printf(TEXT("=== scope: %s ===\n\n"), *IR.ScopeName);
	}

	// Compute topological depth for each node (max distance from a leaf/source node)
	TMap<int32, int32> NodeDepth;
	for (const FMGNodeIR& Node : IR.Nodes)
	{
		NodeDepth.Add(Node.Index, 0);
	}

	TMap<int32, TSet<int32>> Dependents;
	for (const FMGLinkIR& Link : IR.Links)
	{
		if (!Link.bToMaterialOutput)
		{
			Dependents.FindOrAdd(Link.FromNodeIndex).Add(Link.ToNodeIndex);
		}
	}

	bool bChanged = true;
	while (bChanged)
	{
		bChanged = false;
		for (const FMGLinkIR& Link : IR.Links)
		{
			if (Link.bToMaterialOutput)
			{
				continue;
			}

			int32* FromDepth = NodeDepth.Find(Link.FromNodeIndex);
			int32* ToDepth = NodeDepth.Find(Link.ToNodeIndex);
			if (FromDepth && ToDepth)
			{
				int32 Expected = *FromDepth + 1;
				if (*ToDepth < Expected)
				{
					*ToDepth = Expected;
					bChanged = true;
				}
			}
		}
	}

	// Sort nodes by depth, then by original index within same depth
	TArray<int32> SortedIndices;
	SortedIndices.Reserve(IR.Nodes.Num());
	for (int32 i = 0; i < IR.Nodes.Num(); ++i)
	{
		SortedIndices.Add(i);
	}

	SortedIndices.Sort([&IR, &NodeDepth](int32 A, int32 B)
	{
		int32 DepthA = NodeDepth.FindRef(IR.Nodes[A].Index);
		int32 DepthB = NodeDepth.FindRef(IR.Nodes[B].Index);
		if (DepthA != DepthB)
		{
			return DepthA < DepthB;
		}
		return IR.Nodes[A].Index < IR.Nodes[B].Index;
	});

	Result += TEXT("=== nodes ===\n");
	int32 LastDepth = -1;
	for (int32 SortedIdx : SortedIndices)
	{
		const FMGNodeIR& Node = IR.Nodes[SortedIdx];
		int32 Depth = NodeDepth.FindRef(Node.Index);
		if (LastDepth >= 0 && Depth != LastDepth)
		{
			Result += TEXT("\n");
		}
		LastDepth = Depth;

		Result += FString::Printf(TEXT("N%d %s"), Node.Index, *Node.ClassName);

		if (Node.Properties.Num() > 0)
		{
			Result += TEXT(" {");
			bool bFirst = true;
			for (const auto& Pair : Node.Properties)
			{
				if (!bFirst)
				{
					Result += TEXT(", ");
				}
				Result += FString::Printf(TEXT("%s:%s"), *Pair.Key, *Pair.Value);
				bFirst = false;
			}
			Result += TEXT("}");
		}

		if (Node.Guid.IsValid())
		{
			Result += FString::Printf(TEXT(" #%s"), *Node.Guid.ToString(EGuidFormats::DigitsLower));
		}

		Result += TEXT("\n");
	}

	Result += TEXT("\n=== links ===\n");
	for (const FMGLinkIR& Link : IR.Links)
	{
		FString FromStr = FString::Printf(TEXT("N%d"), Link.FromNodeIndex);
		if (!Link.FromOutputName.IsEmpty())
		{
			FromStr += FString::Printf(TEXT(".%s"), *Link.FromOutputName);
		}

		FString ToStr;
		if (Link.bToMaterialOutput)
		{
			ToStr = FString::Printf(TEXT("[%s]"), *Link.ToInputName);
		}
		else
		{
			ToStr = FString::Printf(TEXT("N%d.%s"), Link.ToNodeIndex, *Link.ToInputName);
		}

		Result += FString::Printf(TEXT("%s -> %s\n"), *FromStr, *ToStr);
	}

	return Result;
}

TArray<FString> FMaterialGraphSerializer::ListScopes(UMaterial* Material)
{
	TArray<FString> Scopes;
	if (!Material)
	{
		return Scopes;
	}

	for (int32 i = 0; i < MP_MAX; ++i)
	{
		EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
		FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
		if (Input && Input->Expression)
		{
			const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
			Scopes.Add(AttrName);
		}
	}

#if WITH_EDITORONLY_DATA
	TArray<UMaterialExpressionCustomOutput*> CustomOutputs;
	Material->GetAllCustomOutputExpressions(CustomOutputs);
	for (UMaterialExpressionCustomOutput* CustomOut : CustomOutputs)
	{
		Scopes.Add(CustomOut->GetClass()->GetName());
	}
#endif

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
	for (const TObjectPtr<UMaterialExpression>& Expr : Expressions)
	{
		if (UMaterialExpressionComposite* Composite = Cast<UMaterialExpressionComposite>(Expr.Get()))
		{
			if (!Composite->SubgraphName.IsEmpty())
			{
				Scopes.Add(FString::Printf(TEXT("Composite:%s"), *Composite->SubgraphName));
			}
		}
	}

	return Scopes;
}

TArray<FString> FMaterialGraphSerializer::ListScopes(UMaterialFunction* MaterialFunction)
{
	TArray<FString> Scopes;
	if (!MaterialFunction)
	{
		return Scopes;
	}

	Scopes.Add(TEXT("*"));
	return Scopes;
}
