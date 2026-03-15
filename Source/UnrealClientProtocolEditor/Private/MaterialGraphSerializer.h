// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialFunction;
class UMaterialExpression;
class UMaterialExpressionComposite;
struct FExpressionOutput;

struct FMGNodeIR
{
	int32 Index = -1;
	FString ClassName;
	FGuid Guid;
	TMap<FString, FString> Properties;
	UMaterialExpression* Expression = nullptr;
};

struct FMGLinkIR
{
	int32 FromNodeIndex = -1;
	FString FromOutputName;
	int32 ToNodeIndex = -1;
	FString ToInputName;
	bool bToMaterialOutput = false;
};

struct FMGGraphIR
{
	TArray<FMGNodeIR> Nodes;
	TArray<FMGLinkIR> Links;
	FString ScopeName;
};

class FMaterialGraphSerializer
{
public:
	static FString Serialize(UMaterial* Material, const FString& ScopeName);
	static FString Serialize(UMaterialFunction* MaterialFunction, const FString& ScopeName);

	static FMGGraphIR BuildIR(UMaterial* Material, const FString& ScopeName);
	static FMGGraphIR BuildIR(UMaterialFunction* MaterialFunction, const FString& ScopeName);

	static FString IRToText(const FMGGraphIR& IR);

	static TArray<FString> ListScopes(UMaterial* Material);
	static TArray<FString> ListScopes(UMaterialFunction* MaterialFunction);

private:
	static FMGGraphIR BuildIRFromExpressions(
		TConstArrayView<TObjectPtr<UMaterialExpression>> AllExpressions,
		UMaterial* Material,
		UMaterialFunction* MaterialFunction,
		const FString& ScopeName);

	static void CollectReachableNodes(
		UMaterialExpression* StartExpr,
		TSet<UMaterialExpression*>& OutReachable,
		TSet<UMaterialExpression*>& Visited);

	static void SerializeNodeProperties(
		UMaterialExpression* Expression,
		TMap<FString, FString>& OutProperties);

	static FString GetOutputPinName(
		UMaterialExpression* Expression,
		int32 OutputIndex);

	static FString ShortenInputPinName(const FString& InputName);

	static bool IsConnectionProperty(const FProperty* Prop);
	static bool IsEmbeddedConnectionArrayProperty(const FProperty* Prop);
	static bool ShouldSkipProperty(const FProperty* Prop);

	static const TSet<FName>& GetBaseClassSkipProperties();
};
