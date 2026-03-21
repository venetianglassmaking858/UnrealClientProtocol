// MIT License - Copyright (c) 2025 Italink

#include "Widget/WidgetTreeSectionHandler.h"
#include "Widget/WidgetTreeSerializer.h"

#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogUCPWidgetHandler, Log, All);

static const FString WidgetTreeType = TEXT("WidgetTree");

bool FWidgetTreeSectionHandler::CanHandle(UObject* Asset, const FString& Type) const
{
	if (!Asset || !Asset->IsA<UWidgetBlueprint>())
	{
		return false;
	}

	if (Type.IsEmpty())
	{
		return true;
	}

	return Type == WidgetTreeType;
}

TArray<FNodeCodeSectionIR> FWidgetTreeSectionHandler::ListSections(UObject* Asset)
{
	TArray<FNodeCodeSectionIR> Sections;

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
	if (!WidgetBP)
	{
		return Sections;
	}

	FNodeCodeSectionIR Section;
	Section.Type = WidgetTreeType;
	Sections.Add(MoveTemp(Section));

	return Sections;
}

FNodeCodeSectionIR FWidgetTreeSectionHandler::Read(UObject* Asset, const FString& Type, const FString& Name)
{
	FNodeCodeSectionIR Section;
	Section.Type = Type;
	Section.Name = Name;

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
	if (!WidgetBP)
	{
		return Section;
	}

	Section.RawText = FWidgetTreeSerializer::Serialize(WidgetBP);

	return Section;
}

FNodeCodeDiffResult FWidgetTreeSectionHandler::Write(UObject* Asset, const FNodeCodeSectionIR& Section)
{
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
	if (!WidgetBP)
	{
		return {};
	}

	return FWidgetTreeSerializer::Apply(WidgetBP, Section.RawText);
}

bool FWidgetTreeSectionHandler::CreateSection(UObject* Asset, const FString& Type, const FString& Name)
{
	return false;
}

bool FWidgetTreeSectionHandler::RemoveSection(UObject* Asset, const FString& Type, const FString& Name)
{
	return false;
}
