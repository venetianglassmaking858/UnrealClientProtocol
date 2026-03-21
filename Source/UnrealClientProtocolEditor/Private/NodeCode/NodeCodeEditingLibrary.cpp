// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/NodeCodeEditingLibrary.h"
#include "NodeCode/INodeCodeSectionHandler.h"
#include "NodeCode/NodeCodeTextFormat.h"
#include "NodeCode/NodeCodeClassCache.h"

#include "Blueprint/BlueprintSectionHandler.h"
#include "Material/MaterialSectionHandler.h"
#include "Widget/WidgetTreeSectionHandler.h"

#include "EdGraph/EdGraphNode.h"
#include "Materials/MaterialExpression.h"
#include "Components/Widget.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

DEFINE_LOG_CATEGORY_STATIC(LogNodeCodeEditing, Log, All);

struct FNodeCodeHandlerAutoRegister
{
	FNodeCodeHandlerAutoRegister()
	{
		auto& ClassCache = FNodeCodeClassCache::Get();
		ClassCache.RegisterBaseClass(UEdGraphNode::StaticClass());
		ClassCache.RegisterBaseClass(UMaterialExpression::StaticClass());
		ClassCache.RegisterBaseClass(UWidget::StaticClass());

		auto& Registry = FNodeCodeSectionHandlerRegistry::Get();
		Registry.Register(MakeShared<FBlueprintSectionHandler>());
		Registry.Register(MakeShared<FMaterialSectionHandler>());
		Registry.Register(MakeShared<FWidgetTreeSectionHandler>());
	}
};

static FNodeCodeHandlerAutoRegister GAutoRegisterHandlers;

static UObject* LoadAsset(const FString& AssetPath)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
}

FString UNodeCodeEditingLibrary::Outline(const FString& AssetPath)
{
	UObject* Asset = LoadAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogNodeCodeEditing, Error, TEXT("Outline: Asset not found: %s"), *AssetPath);
		return FString();
	}

	TArray<FNodeCodeSectionIR> Sections = FNodeCodeSectionHandlerRegistry::Get().ListAllSections(Asset);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SectionValues;
	for (const FNodeCodeSectionIR& Section : Sections)
	{
		SectionValues.Add(MakeShared<FJsonValueString>(Section.GetHeader()));
	}
	Root->SetArrayField(TEXT("sections"), SectionValues);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}

FString UNodeCodeEditingLibrary::ReadGraph(const FString& AssetPath, const FString& Section)
{
	UObject* Asset = LoadAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogNodeCodeEditing, Error, TEXT("ReadGraph: Asset not found: %s"), *AssetPath);
		return FString();
	}

	FNodeCodeDocumentIR Document;

	if (Section.IsEmpty())
	{
		TArray<FNodeCodeSectionIR> AllSections = FNodeCodeSectionHandlerRegistry::Get().ListAllSections(Asset);
		for (const FNodeCodeSectionIR& SectionInfo : AllSections)
		{
			INodeCodeSectionHandler* Handler = FNodeCodeSectionHandlerRegistry::Get().FindHandler(Asset, SectionInfo.Type);
			if (Handler)
			{
				Document.Sections.Add(Handler->Read(Asset, SectionInfo.Type, SectionInfo.Name));
			}
		}
	}
	else
	{
		FString Type, Name;
		if (NodeCodeUtils::ParseSectionHeader(FString::Printf(TEXT("[%s]"), *Section), Type, Name))
		{
			INodeCodeSectionHandler* Handler = FNodeCodeSectionHandlerRegistry::Get().FindHandler(Asset, Type);
			if (Handler)
			{
				Document.Sections.Add(Handler->Read(Asset, Type, Name));
			}
			else
			{
				UE_LOG(LogNodeCodeEditing, Error, TEXT("ReadGraph: No handler for section type: %s"), *Type);
			}
		}
		else
		{
			UE_LOG(LogNodeCodeEditing, Error, TEXT("ReadGraph: Invalid section format: %s"), *Section);
		}
	}

	return FNodeCodeTextFormat::DocumentToText(Document);
}

FString UNodeCodeEditingLibrary::WriteGraph(const FString& AssetPath, const FString& Section, const FString& GraphText)
{
	UObject* Asset = LoadAsset(AssetPath);
	if (!Asset)
	{
		UE_LOG(LogNodeCodeEditing, Error, TEXT("WriteGraph: Asset not found: %s"), *AssetPath);
		return FString();
	}

	if (GraphText.IsEmpty() && !Section.IsEmpty())
	{
		FNodeCodeDiffResult RemoveResult;
		FString Type, Name;
		if (NodeCodeUtils::ParseSectionHeader(FString::Printf(TEXT("[%s]"), *Section), Type, Name))
		{
			INodeCodeSectionHandler* Handler = FNodeCodeSectionHandlerRegistry::Get().FindHandler(Asset, Type);
			if (Handler && Handler->RemoveSection(Asset, Type, Name))
			{
				RemoveResult.NodesRemoved.Add(FString::Printf(TEXT("Removed section: %s"), *Section));
			}
			else
			{
				RemoveResult.NodesModified.Add(FString::Printf(TEXT("Failed to remove section: %s"), *Section));
			}
		}
		return FNodeCodeTextFormat::DiffResultToJson(RemoveResult);
	}

	FNodeCodeDocumentIR Document;

	if (!Section.IsEmpty())
	{
		FString Type, Name;
		if (NodeCodeUtils::ParseSectionHeader(FString::Printf(TEXT("[%s]"), *Section), Type, Name))
		{
			FNodeCodeSectionIR SectionIR = FNodeCodeTextFormat::ParseSection(GraphText, Type, Name);
			Document.Sections.Add(MoveTemp(SectionIR));
		}
	}
	else
	{
		Document = FNodeCodeTextFormat::ParseDocument(GraphText);
	}

	FNodeCodeDiffResult CombinedResult;

	for (const FNodeCodeSectionIR& SectionIR : Document.Sections)
	{
		INodeCodeSectionHandler* Handler = FNodeCodeSectionHandlerRegistry::Get().FindHandler(Asset, SectionIR.Type);
		if (!Handler)
		{
			UE_LOG(LogNodeCodeEditing, Error, TEXT("WriteGraph: No handler for section type: %s"), *SectionIR.Type);
			continue;
		}

		bool bSectionExists = false;
		TArray<FNodeCodeSectionIR> ExistingSections = Handler->ListSections(Asset);
		for (const FNodeCodeSectionIR& Existing : ExistingSections)
		{
			if (Existing.Type == SectionIR.Type && Existing.Name == SectionIR.Name)
			{
				bSectionExists = true;
				break;
			}
		}

		if (!bSectionExists && SectionIR.IsGraphSection())
		{
			if (!Handler->CreateSection(Asset, SectionIR.Type, SectionIR.Name))
			{
				UE_LOG(LogNodeCodeEditing, Warning, TEXT("WriteGraph: Could not create section [%s:%s]"), *SectionIR.Type, *SectionIR.Name);
			}
		}

		FNodeCodeDiffResult SectionResult = Handler->Write(Asset, SectionIR);

		CombinedResult.NodesAdded.Append(SectionResult.NodesAdded);
		CombinedResult.NodesRemoved.Append(SectionResult.NodesRemoved);
		CombinedResult.NodesModified.Append(SectionResult.NodesModified);
		CombinedResult.LinksAdded.Append(SectionResult.LinksAdded);
		CombinedResult.LinksRemoved.Append(SectionResult.LinksRemoved);
	}

	return FNodeCodeTextFormat::DiffResultToJson(CombinedResult);
}
