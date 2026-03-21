// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"

struct FNodeCodeNodeIR
{
	int32 Index = -1;
	FString ClassName;
	FGuid Guid;
	TMap<FString, FString> Properties;
	UObject* SourceObject = nullptr;
};

struct FNodeCodeLinkIR
{
	int32 FromNodeIndex = -1;
	FString FromOutputName;
	int32 ToNodeIndex = -1;
	FString ToInputName;
	bool bToGraphOutput = false;
};

struct FNodeCodeGraphIR
{
	TArray<FNodeCodeNodeIR> Nodes;
	TArray<FNodeCodeLinkIR> Links;
};

struct FNodeCodeSectionIR
{
	FString Type;
	FString Name;

	FNodeCodeGraphIR Graph;

	TMap<FString, FString> Properties;

	// For sections with custom text format (e.g. WidgetTree's indentation-based tree)
	FString RawText;

	bool IsGraphSection() const { return Type != TEXT("Properties") && Type != TEXT("Variables") && Type != TEXT("WidgetTree"); }

	bool IsRawTextSection() const { return Type == TEXT("WidgetTree"); }

	FString GetHeader() const
	{
		if (Name.IsEmpty())
		{
			return FString::Printf(TEXT("[%s]"), *Type);
		}
		return FString::Printf(TEXT("[%s:%s]"), *Type, *Name);
	}
};

struct FNodeCodeDocumentIR
{
	TArray<FNodeCodeSectionIR> Sections;
};

namespace NodeCodeUtils
{
	inline FString EncodeSpaces(const FString& InStr)
	{
		return InStr.Replace(TEXT(" "), TEXT("_"));
	}

	inline bool MatchName(const FString& Encoded, const FString& Original)
	{
		if (Encoded == Original)
		{
			return true;
		}
		return Encoded.Replace(TEXT("_"), TEXT(" ")) == Original
			|| Encoded == Original.Replace(TEXT(" "), TEXT("_"));
	}

	inline bool ParseSectionHeader(const FString& Header, FString& OutType, FString& OutName)
	{
		FString Inner = Header;
		if (!Inner.RemoveFromStart(TEXT("[")) || !Inner.RemoveFromEnd(TEXT("]")))
		{
			return false;
		}
		Inner.TrimStartAndEndInline();
		int32 ColonPos;
		if (Inner.FindChar(':', ColonPos))
		{
			OutType = Inner.Left(ColonPos);
			OutName = Inner.Mid(ColonPos + 1);
		}
		else
		{
			OutType = Inner;
			OutName.Empty();
		}
		return !OutType.IsEmpty();
	}
}

struct FNodeCodeDiffResult
{
	TArray<FString> NodesAdded;
	TArray<FString> NodesRemoved;
	TArray<FString> NodesModified;
	TArray<FString> LinksAdded;
	TArray<FString> LinksRemoved;
};
