// MIT License - Copyright (c) 2025 Italink

#include "MaterialGraphDiffer.h"
#include "MaterialExpressionClassCache.h"
#include "MaterialGraphSerializer.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "Materials/MaterialExpressionRerouteBase.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionSwitch.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "MaterialEditingLibrary.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"
#include "IMaterialEditor.h"

DEFINE_LOG_CATEGORY_STATIC(LogUCP, Log, All);

// ---- Text Parser ----

static FString TrimLine(const FString& Line)
{
	FString Result = Line;
	Result.TrimStartAndEndInline();
	return Result;
}

static bool ParseNodeLine(const FString& Line, int32& OutIndex, FString& OutClassName, TMap<FString, FString>& OutProps, FGuid& OutGuid)
{
	// Format: N<index> <ClassName> {Key:Value, ...} #<guid>
	// or:     N<index> <ClassName> #<guid>
	// or:     N<index> <ClassName> {Key:Value, ...}
	// or:     N<index> <ClassName>

	FString Working = Line;

	// Parse guid from end
	OutGuid.Invalidate();
	int32 HashPos;
	if (Working.FindLastChar('#', HashPos))
	{
		FString GuidStr = Working.Mid(HashPos + 1).TrimStartAndEnd();
		if (GuidStr.Len() >= 32)
		{
			FGuid::Parse(GuidStr, OutGuid);
		}
		Working = Working.Left(HashPos).TrimEnd();
	}

	// Parse properties block
	int32 BraceOpen, BraceClose;
	if (Working.FindChar('{', BraceOpen) && Working.FindLastChar('}', BraceClose) && BraceClose > BraceOpen)
	{
		FString PropsStr = Working.Mid(BraceOpen + 1, BraceClose - BraceOpen - 1);
		Working = Working.Left(BraceOpen).TrimEnd();

		// Parse key:value pairs (handle nested quotes and parentheses)
		int32 Depth = 0;
		bool bInQuote = false;
		int32 Start = 0;

		auto ParsePair = [&](const FString& PairStr)
		{
			int32 ColonPos;
			if (PairStr.FindChar(':', ColonPos))
			{
				FString Key = PairStr.Left(ColonPos).TrimStartAndEnd();
				FString Value = PairStr.Mid(ColonPos + 1).TrimStartAndEnd();
				if (!Key.IsEmpty())
				{
					OutProps.Add(Key, Value);
				}
			}
		};

		for (int32 i = 0; i < PropsStr.Len(); ++i)
		{
			TCHAR Ch = PropsStr[i];
			if (Ch == '"')
			{
				bInQuote = !bInQuote;
			}
			else if (!bInQuote)
			{
				if (Ch == '(' || Ch == '[')
				{
					Depth++;
				}
				else if (Ch == ')' || Ch == ']')
				{
					Depth--;
				}
				else if (Ch == ',' && Depth == 0)
				{
					ParsePair(PropsStr.Mid(Start, i - Start));
					Start = i + 1;
				}
			}
		}
		if (Start < PropsStr.Len())
		{
			ParsePair(PropsStr.Mid(Start));
		}
	}

	// Parse N<index> <ClassName>
	int32 SpacePos;
	if (!Working.FindChar(' ', SpacePos))
	{
		return false;
	}

	FString IndexStr = Working.Left(SpacePos);
	if (!IndexStr.StartsWith(TEXT("N")))
	{
		return false;
	}

	OutIndex = FCString::Atoi(*IndexStr.Mid(1));
	OutClassName = Working.Mid(SpacePos + 1).TrimStartAndEnd();

	return !OutClassName.IsEmpty();
}

static bool ParseLinkLine(const FString& Line, int32& OutFromNode, FString& OutFromOutput,
	int32& OutToNode, FString& OutToInput, bool& bOutToMaterialOutput)
{
	// Format: N<from>[.OutputName] -> N<to>.InputName
	//     or: N<from>[.OutputName] -> [MaterialOutputName]

	int32 ArrowPos = Line.Find(TEXT("->"));
	if (ArrowPos == INDEX_NONE)
	{
		return false;
	}

	FString FromStr = Line.Left(ArrowPos).TrimEnd();
	FString ToStr = Line.Mid(ArrowPos + 2).TrimStart();

	// Parse From
	int32 DotPos;
	if (FromStr.FindChar('.', DotPos))
	{
		FString NodeStr = FromStr.Left(DotPos);
		if (!NodeStr.StartsWith(TEXT("N")))
		{
			return false;
		}
		OutFromNode = FCString::Atoi(*NodeStr.Mid(1));
		OutFromOutput = FromStr.Mid(DotPos + 1);
	}
	else
	{
		if (!FromStr.StartsWith(TEXT("N")))
		{
			return false;
		}
		OutFromNode = FCString::Atoi(*FromStr.Mid(1));
		OutFromOutput = FString();
	}

	// Parse To
	if (ToStr.StartsWith(TEXT("[")) && ToStr.EndsWith(TEXT("]")))
	{
		bOutToMaterialOutput = true;
		OutToInput = ToStr.Mid(1, ToStr.Len() - 2);
		OutToNode = -1;
	}
	else
	{
		bOutToMaterialOutput = false;
		if (ToStr.FindChar('.', DotPos))
		{
			FString NodeStr = ToStr.Left(DotPos);
			if (!NodeStr.StartsWith(TEXT("N")))
			{
				return false;
			}
			OutToNode = FCString::Atoi(*NodeStr.Mid(1));
			OutToInput = ToStr.Mid(DotPos + 1);
		}
		else
		{
			return false;
		}
	}

	return true;
}

FMGGraphIR FMaterialGraphDiffer::ParseText(const FString& GraphText)
{
	FMGGraphIR IR;

	TArray<FString> Lines;
	GraphText.ParseIntoArrayLines(Lines);

	enum class ESection { None, Nodes, Links };
	ESection CurrentSection = ESection::None;

	for (const FString& RawLine : Lines)
	{
		FString Line = TrimLine(RawLine);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		if (Line.StartsWith(TEXT("=== scope:")))
		{
			int32 ColonPos = Line.Find(TEXT(":"));
			int32 EndPos = Line.Find(TEXT("==="), ESearchCase::IgnoreCase, ESearchDir::FromStart, ColonPos + 1);
			if (ColonPos != INDEX_NONE && EndPos != INDEX_NONE)
			{
				IR.ScopeName = Line.Mid(ColonPos + 1, EndPos - ColonPos - 1).TrimStartAndEnd();
			}
			continue;
		}

		if (Line == TEXT("=== nodes ==="))
		{
			CurrentSection = ESection::Nodes;
			continue;
		}
		if (Line == TEXT("=== links ==="))
		{
			CurrentSection = ESection::Links;
			continue;
		}

		if (CurrentSection == ESection::Nodes)
		{
			FMGNodeIR Node;
			if (ParseNodeLine(Line, Node.Index, Node.ClassName, Node.Properties, Node.Guid))
			{
				IR.Nodes.Add(MoveTemp(Node));
			}
		}
		else if (CurrentSection == ESection::Links)
		{
			FMGLinkIR Link;
			if (ParseLinkLine(Line, Link.FromNodeIndex, Link.FromOutputName,
				Link.ToNodeIndex, Link.ToInputName, Link.bToMaterialOutput))
			{
				IR.Links.Add(MoveTemp(Link));
			}
		}
	}

	return IR;
}

// ---- Diff & Apply ----

FMGDiffResult FMaterialGraphDiffer::Apply(UMaterial* Material, const FString& ScopeName, const FString& GraphText)
{
	if (!Material)
	{
		FMGDiffResult R;
		UE_LOG(LogUCP, Error, TEXT("WriteGraph: Material is null"));
		return R;
	}

	FMGGraphIR NewIR = ParseText(GraphText);
	return DiffAndApply(Material, nullptr, ScopeName, NewIR);
}

FMGDiffResult FMaterialGraphDiffer::Apply(UMaterialFunction* MaterialFunction, const FString& ScopeName, const FString& GraphText)
{
	if (!MaterialFunction)
	{
		FMGDiffResult R;
		UE_LOG(LogUCP, Error, TEXT("WriteGraph: MaterialFunction is null"));
		return R;
	}

	FMGGraphIR NewIR = ParseText(GraphText);
	return DiffAndApply(nullptr, MaterialFunction, ScopeName, NewIR);
}

void FMaterialGraphDiffer::MatchNodes(
	const FMGGraphIR& OldIR,
	const FMGGraphIR& NewIR,
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

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FMGNodeIR& NewNode = NewIR.Nodes[NewIdx];

		if (NewNode.Guid.IsValid())
		{
			if (int32* OldIdx = OldGuidMap.Find(NewNode.Guid))
			{
				if (!MatchedOld.Contains(*OldIdx))
				{
					OutNewToOld.Add(NewIdx, *OldIdx);
					MatchedOld.Add(*OldIdx);
					continue;
				}
			}
		}

		auto GetKeyProp = [](const FMGNodeIR& N) -> FString
		{
			if (const FString* Val = N.Properties.Find(TEXT("ParameterName")))
			{
				return *Val;
			}
			if (const FString* Val = N.Properties.Find(TEXT("Texture")))
			{
				return *Val;
			}
			if (const FString* Val = N.Properties.Find(TEXT("MaterialFunction")))
			{
				return *Val;
			}
			return FString();
		};

		FString NewKey = GetKeyProp(NewNode);

		for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
		{
			if (MatchedOld.Contains(OldIdx))
			{
				continue;
			}

			const FMGNodeIR& OldNode = OldIR.Nodes[OldIdx];
			if (OldNode.ClassName != NewNode.ClassName)
			{
				continue;
			}

			FString OldKey = GetKeyProp(OldNode);

			if (!OldKey.IsEmpty() && OldKey == NewKey)
			{
				OutNewToOld.Add(NewIdx, OldIdx);
				MatchedOld.Add(OldIdx);
				break;
			}
		}
	}
}

void FMaterialGraphDiffer::ApplyPropertyChanges(
	UMaterialExpression* Expression,
	const TMap<FString, FString>& NewProperties,
	TArray<FString>& OutChanges)
{
	UClass* ExprClass = Expression->GetClass();

	if (UMaterialExpressionCustom* CustomExpr = Cast<UMaterialExpressionCustom>(Expression))
	{
		if (const FString* InputNamesStr = NewProperties.Find(TEXT("InputNames")))
		{
			FString Working = *InputNamesStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> Names;
			Working.ParseIntoArray(Names, TEXT(","));

			CustomExpr->Inputs.Empty();
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				if (Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\"")))
				{
					Name = Name.Mid(1, Name.Len() - 2);
				}
				FCustomInput NewInput;
				NewInput.InputName = FName(*Name);
				CustomExpr->Inputs.Add(MoveTemp(NewInput));
			}

			if (CustomExpr->Inputs.Num() == 0)
			{
				FCustomInput DefaultInput;
				DefaultInput.InputName = NAME_None;
				CustomExpr->Inputs.Add(MoveTemp(DefaultInput));
			}

			CustomExpr->RebuildOutputs();
			OutChanges.Add(FString::Printf(TEXT("InputNames: %d inputs"), CustomExpr->Inputs.Num()));
		}
	}

	if (UMaterialExpressionSwitch* SwitchExpr = Cast<UMaterialExpressionSwitch>(Expression))
	{
		if (const FString* SwitchNamesStr = NewProperties.Find(TEXT("SwitchInputNames")))
		{
			FString Working = *SwitchNamesStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> Names;
			Working.ParseIntoArray(Names, TEXT(","));

			SwitchExpr->Inputs.Empty();
			for (FString& Name : Names)
			{
				Name.TrimStartAndEndInline();
				if (Name.StartsWith(TEXT("\"")) && Name.EndsWith(TEXT("\"")))
				{
					Name = Name.Mid(1, Name.Len() - 2);
				}
				FSwitchCustomInput NewInput;
				NewInput.InputName = FName(*Name);
				SwitchExpr->Inputs.Add(MoveTemp(NewInput));
			}

			OutChanges.Add(FString::Printf(TEXT("SwitchInputNames: %d cases"), SwitchExpr->Inputs.Num()));
		}
	}

	if (UMaterialExpressionSetMaterialAttributes* SetAttrExpr = Cast<UMaterialExpressionSetMaterialAttributes>(Expression))
	{
		if (const FString* AttrsStr = NewProperties.Find(TEXT("Attributes")))
		{
			FString Working = *AttrsStr;
			Working.TrimStartAndEndInline();
			if (Working.StartsWith(TEXT("[")) && Working.EndsWith(TEXT("]")))
			{
				Working = Working.Mid(1, Working.Len() - 2);
			}

			TArray<FString> AttrNames;
			Working.ParseIntoArray(AttrNames, TEXT(","));

			SetAttrExpr->AttributeSetTypes.Empty();
			SetAttrExpr->Inputs.Empty();
			SetAttrExpr->Inputs.Add(FExpressionInput());

			for (FString& AttrName : AttrNames)
			{
				AttrName.TrimStartAndEndInline();
				if (AttrName.StartsWith(TEXT("\"")) && AttrName.EndsWith(TEXT("\"")))
				{
					AttrName = AttrName.Mid(1, AttrName.Len() - 2);
				}

				FGuid AttrGuid;
				for (int32 i = 0; i < MP_MAX; ++i)
				{
					EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
					const FString& Name = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
					if (Name == AttrName)
					{
						AttrGuid = FMaterialAttributeDefinitionMap::GetID(Prop);
						break;
					}
				}

				if (!AttrGuid.IsValid())
				{
					AttrGuid = FMaterialAttributeDefinitionMap::GetCustomAttributeID(AttrName);
				}

				if (AttrGuid.IsValid())
				{
					SetAttrExpr->AttributeSetTypes.Add(AttrGuid);
					SetAttrExpr->Inputs.Add(FExpressionInput());
				}
				else
				{
					OutChanges.Add(FString::Printf(TEXT("Unknown attribute: %s"), *AttrName));
				}
			}

			OutChanges.Add(FString::Printf(TEXT("Attributes: %d attributes"), SetAttrExpr->AttributeSetTypes.Num()));
		}
	}

	for (const auto& Pair : NewProperties)
	{
		if (Pair.Key == TEXT("InputNames") || Pair.Key == TEXT("SwitchInputNames") || Pair.Key == TEXT("Attributes"))
		{
			continue;
		}

		FProperty* Prop = ExprClass->FindPropertyByName(FName(*Pair.Key));
		if (!Prop)
		{
			OutChanges.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Expression);

		FString OldValue;
		Prop->ExportTextItem_Direct(OldValue, ValuePtr, nullptr, Expression, PPF_None, nullptr);

		FString ImportValue = Pair.Value;
		if (ImportValue.StartsWith(TEXT("\"")) && ImportValue.EndsWith(TEXT("\"")))
		{
			ImportValue = ImportValue.Mid(1, ImportValue.Len() - 2);
			ImportValue = ImportValue.ReplaceEscapedCharWithChar();
		}

		const TCHAR* Buffer = *ImportValue;
		const TCHAR* Result = Prop->ImportText_Direct(Buffer, ValuePtr, Expression, PPF_None, GWarn);

		if (Result)
		{
			FString NewValue;
			Prop->ExportTextItem_Direct(NewValue, ValuePtr, nullptr, Expression, PPF_None, nullptr);
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

EMaterialProperty FMaterialGraphDiffer::FindMaterialPropertyByName(UMaterial* Material, const FString& Name)
{
	for (int32 i = 0; i < MP_MAX; ++i)
	{
		EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
		const FString& AttrName = FMaterialAttributeDefinitionMap::GetAttributeName(Prop);
		if (AttrName == Name)
		{
			return Prop;
		}
	}
	return MP_MAX;
}

FMGDiffResult FMaterialGraphDiffer::DiffAndApply(
	UMaterial* Material,
	UMaterialFunction* MaterialFunction,
	const FString& ScopeName,
	const FMGGraphIR& NewIR)
{
	FMGDiffResult Result;
	Result.bSuccess = true;

	FMaterialExpressionClassCache::Get().Build();

	FMGGraphIR OldIR;
	if (Material)
	{
		OldIR = FMaterialGraphSerializer::BuildIR(Material, ScopeName);
	}
	else if (MaterialFunction)
	{
		OldIR = FMaterialGraphSerializer::BuildIR(MaterialFunction, ScopeName);
	}

	TMap<int32, int32> NewToOld;
	MatchNodes(OldIR, NewIR, NewToOld);

	TSet<int32> MatchedOldIndices;
	for (auto& Pair : NewToOld)
	{
		MatchedOldIndices.Add(Pair.Value);
	}

	FScopedTransaction Transaction(NSLOCTEXT("UCPMaterialGraph", "WriteGraph", "UCP: Write Material Graph"));

	// Phase 1: Delete removed nodes
	for (int32 OldIdx = 0; OldIdx < OldIR.Nodes.Num(); ++OldIdx)
	{
		if (MatchedOldIndices.Contains(OldIdx))
		{
			continue;
		}

		const FMGNodeIR& OldNode = OldIR.Nodes[OldIdx];
		if (OldNode.Expression)
		{
			if (Material)
			{
				UMaterialEditingLibrary::DeleteMaterialExpression(Material, OldNode.Expression);
			}
			else if (MaterialFunction)
			{
				UMaterialEditingLibrary::DeleteMaterialExpressionInFunction(MaterialFunction, OldNode.Expression);
			}
			Result.NodesRemoved.Add(FString::Printf(TEXT("N%d %s"), OldNode.Index, *OldNode.ClassName));
			Result.bRelayout = true;
		}
	}

	// Phase 2: Create new nodes and map NewIndex -> Expression
	TMap<int32, UMaterialExpression*> NewIndexToExpr;

	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		const FMGNodeIR& NewNode = NewIR.Nodes[NewIdx];

		if (int32* OldIdx = NewToOld.Find(NewIdx))
		{
			NewIndexToExpr.Add(NewNode.Index, OldIR.Nodes[*OldIdx].Expression);
		}
		else
		{
			UClass* ExprClass = FMaterialExpressionClassCache::Get().FindClass(NewNode.ClassName);
			if (!ExprClass)
			{
				UE_LOG(LogUCP, Error, TEXT("WriteGraph: Unknown expression class: %s"), *NewNode.ClassName);
				Result.bSuccess = false;
				continue;
			}

			UMaterialExpression* NewExpr = nullptr;
			if (Material)
			{
				NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Material, ExprClass, 0, 0);
			}
			else if (MaterialFunction)
			{
				NewExpr = UMaterialEditingLibrary::CreateMaterialExpressionInFunction(MaterialFunction, ExprClass, 0, 0);
			}

			if (!NewExpr)
			{
				UE_LOG(LogUCP, Error, TEXT("WriteGraph: Failed to create expression: %s"), *NewNode.ClassName);
				Result.bSuccess = false;
				continue;
			}

			TArray<FString> Changes;
			ApplyPropertyChanges(NewExpr, NewNode.Properties, Changes);

			NewIndexToExpr.Add(NewNode.Index, NewExpr);
			Result.NodesAdded.Add(FString::Printf(TEXT("N%d %s"), NewNode.Index, *NewNode.ClassName));
			Result.bRelayout = true;
		}
	}

	// Phase 3: Modify properties on matched nodes
	for (int32 NewIdx = 0; NewIdx < NewIR.Nodes.Num(); ++NewIdx)
	{
		int32* OldIdx = NewToOld.Find(NewIdx);
		if (!OldIdx)
		{
			continue;
		}

		const FMGNodeIR& NewNode = NewIR.Nodes[NewIdx];
		const FMGNodeIR& OldNode = OldIR.Nodes[*OldIdx];
		UMaterialExpression* Expr = OldNode.Expression;

		if (!Expr)
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
			Expr->PreEditChange(nullptr);

			TArray<FString> Changes;
			ApplyPropertyChanges(Expr, NewNode.Properties, Changes);

			FPropertyChangedEvent ChangedEvent(nullptr);
			Expr->PostEditChangeProperty(ChangedEvent);

			if (Changes.Num() > 0)
			{
				Result.NodesModified.Add(FString::Printf(TEXT("N%d: %s"),
					NewNode.Index, *FString::Join(Changes, TEXT("; "))));
			}
		}
	}

	// Phase 4: Clear all existing connections on nodes in scope, then rebuild
	for (auto& Pair : NewIndexToExpr)
	{
		UMaterialExpression* Expr = Pair.Value;
		if (!Expr)
		{
			continue;
		}

		for (FExpressionInputIterator It(Expr); It; ++It)
		{
			if (It->Expression)
			{
				Result.LinksRemoved.Add(FString::Printf(TEXT("(cleared input on N%d)"), Pair.Key));
				It->Expression = nullptr;
			}
		}
	}

	if (Material)
	{
		for (int32 i = 0; i < MP_MAX; ++i)
		{
			EMaterialProperty Prop = static_cast<EMaterialProperty>(i);
			FExpressionInput* Input = Material->GetExpressionInputForProperty(Prop);
			if (Input && Input->Expression && NewIndexToExpr.FindKey(Input->Expression))
			{
				Input->Expression = nullptr;
			}
		}
	}

	// Phase 5: Rebuild connections from NewIR using low-level ConnectExpression API
	// This avoids GetShortenPinName mapping issues in ConnectMaterialExpressions
	auto FindInputIndexByName = [](UMaterialExpression* Expr, const FString& InputName) -> int32
	{
		if (InputName.IsEmpty())
		{
			return (Expr->GetInput(0) != nullptr) ? 0 : INDEX_NONE;
		}

		// Primary: match by GetInputName (display name, what ReadGraph serializes)
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (Expr->GetInputName(i).ToString() == InputName)
			{
				return i;
			}
		}

		// Fallback: match by UPROPERTY field name (stable, not affected by GetInputName overrides)
		UClass* ExprClass = Expr->GetClass();
		for (TFieldIterator<FStructProperty> PropIt(ExprClass); PropIt; ++PropIt)
		{
			FStructProperty* StructProp = *PropIt;
			if (!StructProp || !StructProp->Struct)
			{
				continue;
			}

			const UStruct* Current = StructProp->Struct;
			bool bIsInput = false;
			while (Current)
			{
				if (Current->GetFName() == NAME_ExpressionInput)
				{
					bIsInput = true;
					break;
				}
				Current = Current->GetSuperStruct();
			}

			if (bIsInput && StructProp->GetName() == InputName)
			{
				const void* InputPtr = StructProp->ContainerPtrToValuePtr<void>(Expr);
				for (int32 i = 0; ; ++i)
				{
					FExpressionInput* Input = Expr->GetInput(i);
					if (!Input)
					{
						break;
					}
					if (Input == InputPtr)
					{
						return i;
					}
				}
			}
		}

		return INDEX_NONE;
	};

	auto FindOutputIndexByName = [](UMaterialExpression* Expr, const FString& OutputName) -> int32
	{
		if (OutputName.IsEmpty())
		{
			return 0;
		}

		TArray<FExpressionOutput>& Outputs = Expr->GetOutputs();
		for (int32 i = 0; i < Outputs.Num(); ++i)
		{
			const FExpressionOutput& Output = Outputs[i];
			if (!Output.OutputName.IsNone() && Output.OutputName.ToString() == OutputName)
			{
				return i;
			}

			if (Output.MaskR && !Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("R")) return i;
			if (!Output.MaskR && Output.MaskG && !Output.MaskB && !Output.MaskA && OutputName == TEXT("G")) return i;
			if (!Output.MaskR && !Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == TEXT("B")) return i;
			if (!Output.MaskR && !Output.MaskG && !Output.MaskB && Output.MaskA && OutputName == TEXT("A")) return i;
			if (Output.MaskR && Output.MaskG && Output.MaskB && !Output.MaskA && OutputName == TEXT("RGB")) return i;
			if (Output.MaskR && Output.MaskG && Output.MaskB && Output.MaskA && OutputName == TEXT("RGBA")) return i;
		}

		return INDEX_NONE;
	};

	for (const FMGLinkIR& Link : NewIR.Links)
	{
		UMaterialExpression** FromExprPtr = NewIndexToExpr.Find(Link.FromNodeIndex);
		if (!FromExprPtr || !*FromExprPtr)
		{
			UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Link source N%d not found"), Link.FromNodeIndex);
			continue;
		}

		int32 FromOutputIndex = FindOutputIndexByName(*FromExprPtr, Link.FromOutputName);
		if (FromOutputIndex == INDEX_NONE)
		{
			UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Output '%s' not found on N%d"), *Link.FromOutputName, Link.FromNodeIndex);
			continue;
		}

		if (Link.bToMaterialOutput && Material)
		{
			EMaterialProperty MatProp = FindMaterialPropertyByName(Material, Link.ToInputName);
			if (MatProp != MP_MAX)
			{
				FExpressionInput* MatInput = Material->GetExpressionInputForProperty(MatProp);
				if (MatInput)
				{
					(*FromExprPtr)->ConnectExpression(MatInput, FromOutputIndex);
				}
				Result.LinksAdded.Add(FString::Printf(TEXT("N%d%s -> [%s]"),
					Link.FromNodeIndex,
					Link.FromOutputName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(".%s"), *Link.FromOutputName),
					*Link.ToInputName));
			}
			else
			{
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Unknown material output: %s"), *Link.ToInputName);
			}
		}
		else if (!Link.bToMaterialOutput)
		{
			UMaterialExpression** ToExprPtr = NewIndexToExpr.Find(Link.ToNodeIndex);
			if (!ToExprPtr || !*ToExprPtr)
			{
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Link target N%d not found"), Link.ToNodeIndex);
				continue;
			}

			int32 ToInputIndex = FindInputIndexByName(*ToExprPtr, Link.ToInputName);
			if (ToInputIndex == INDEX_NONE)
			{
				FString AvailableInputs;
				for (int32 DbgIdx = 0; ; ++DbgIdx)
				{
					FExpressionInput* DbgInput = (*ToExprPtr)->GetInput(DbgIdx);
					if (!DbgInput) break;
					if (!AvailableInputs.IsEmpty()) AvailableInputs += TEXT(", ");
					AvailableInputs += (*ToExprPtr)->GetInputName(DbgIdx).ToString();
				}
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Input '%s' not found on N%d (%s). Available: [%s]"),
					*Link.ToInputName, Link.ToNodeIndex, *(*ToExprPtr)->GetClass()->GetName(), *AvailableInputs);
				continue;
			}

			FExpressionInput* ToInput = (*ToExprPtr)->GetInput(ToInputIndex);
			if (ToInput)
			{
				(*FromExprPtr)->ConnectExpression(ToInput, FromOutputIndex);
				Result.LinksAdded.Add(FString::Printf(TEXT("N%d%s -> N%d.%s"),
					Link.FromNodeIndex,
					Link.FromOutputName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(".%s"), *Link.FromOutputName),
					Link.ToNodeIndex,
					*Link.ToInputName));
			}
			else
			{
				UE_LOG(LogUCP, Warning, TEXT("WriteGraph: Failed to get input %d on N%d"), ToInputIndex, Link.ToNodeIndex);
			}
		}
	}

	// Phase 6: Relayout if needed, then recompile and refresh editor UI
	if (Result.bRelayout)
	{
		if (Material)
		{
			UMaterialEditingLibrary::LayoutMaterialExpressions(Material);
		}
		else if (MaterialFunction)
		{
			UMaterialEditingLibrary::LayoutMaterialFunctionExpressions(MaterialFunction);
		}
	}

	if (Material)
	{
		UMaterialEditingLibrary::RecompileMaterial(Material);

		if (GEditor)
		{
			if (IAssetEditorInstance* EditorInstance = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Material, false))
			{
				if (!Material->IsA(UMaterialInstanceDynamic::StaticClass()))
				{
					IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
					MaterialEditor->UpdateMaterialAfterGraphChange();
				}
			}
		}
	}
	else if (MaterialFunction)
	{
		UMaterialEditingLibrary::UpdateMaterialFunction(MaterialFunction, nullptr);
	}

	return Result;
}

FString FMaterialGraphDiffer::DiffResultToJson(const FMGDiffResult& Result)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Result.bSuccess);

	TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();

	auto ToJsonArray = [](const TArray<FString>& Arr) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& S : Arr)
		{
			Values.Add(MakeShared<FJsonValueString>(S));
		}
		return Values;
	};

	Diff->SetArrayField(TEXT("nodes_added"), ToJsonArray(Result.NodesAdded));
	Diff->SetArrayField(TEXT("nodes_removed"), ToJsonArray(Result.NodesRemoved));
	Diff->SetArrayField(TEXT("nodes_modified"), ToJsonArray(Result.NodesModified));
	Diff->SetArrayField(TEXT("links_added"), ToJsonArray(Result.LinksAdded));
	Diff->SetArrayField(TEXT("links_removed"), ToJsonArray(Result.LinksRemoved));
	Root->SetObjectField(TEXT("diff"), Diff);

	Root->SetBoolField(TEXT("relayout"), Result.bRelayout);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
