// MIT License - Copyright (c) 2025 Italink

#include "NodeCode/NodeCodeTextFormat.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// ---- Node Line Parsing ----

static FString TrimLine(const FString& Line)
{
	FString Result = Line;
	Result.TrimStartAndEndInline();
	return Result;
}

bool FNodeCodeTextFormat::ParseNodeLine(const FString& Line, FNodeCodeNodeIR& OutNode)
{
	FString Working = Line;

	OutNode.Guid.Invalidate();
	int32 HashPos;
	if (Working.FindLastChar('#', HashPos))
	{
		FString GuidStr = Working.Mid(HashPos + 1).TrimStartAndEnd();
		if (GuidStr.Len() >= 32)
		{
			FGuid::Parse(GuidStr, OutNode.Guid);
		}
		Working = Working.Left(HashPos).TrimEnd();
	}

	int32 BraceOpen, BraceClose;
	if (Working.FindChar('{', BraceOpen) && Working.FindLastChar('}', BraceClose) && BraceClose > BraceOpen)
	{
		FString PropsStr = Working.Mid(BraceOpen + 1, BraceClose - BraceOpen - 1);
		Working = Working.Left(BraceOpen).TrimEnd();

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
					OutNode.Properties.Add(Key, Value);
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
				if (Ch == '(' || Ch == '[' || Ch == '{')
				{
					Depth++;
				}
				else if (Ch == ')' || Ch == ']' || Ch == '}')
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

	OutNode.Index = FCString::Atoi(*IndexStr.Mid(1));
	OutNode.ClassName = Working.Mid(SpacePos + 1).TrimStartAndEnd();

	return !OutNode.ClassName.IsEmpty();
}

// ---- Link Line Parsing ----

bool FNodeCodeTextFormat::ParseLinkLine(const FString& Line, int32 OwnerNodeIndex, FNodeCodeLinkIR& OutLink)
{
	FString Working = Line.TrimStartAndEnd();
	if (!Working.RemoveFromStart(TEXT(">")))
	{
		return false;
	}
	Working.TrimStartInline();

	int32 ArrowPos = Working.Find(TEXT("->"));
	if (ArrowPos == INDEX_NONE)
	{
		return false;
	}

	FString FromStr = Working.Left(ArrowPos).TrimEnd();
	FString ToStr = Working.Mid(ArrowPos + 2).TrimStart();

	OutLink.FromNodeIndex = OwnerNodeIndex;
	if (!FromStr.IsEmpty())
	{
		OutLink.FromOutputName = FromStr;
	}
	else
	{
		OutLink.FromOutputName.Empty();
	}

	if (ToStr.StartsWith(TEXT("[")) && ToStr.EndsWith(TEXT("]")))
	{
		OutLink.bToGraphOutput = true;
		OutLink.ToInputName = ToStr.Mid(1, ToStr.Len() - 2);
		OutLink.ToNodeIndex = -1;
	}
	else
	{
		OutLink.bToGraphOutput = false;
		int32 DotPos;
		if (ToStr.FindChar('.', DotPos))
		{
			FString NodeStr = ToStr.Left(DotPos);
			if (!NodeStr.StartsWith(TEXT("N")))
			{
				return false;
			}
			OutLink.ToNodeIndex = FCString::Atoi(*NodeStr.Mid(1));
			OutLink.ToInputName = ToStr.Mid(DotPos + 1);
		}
		else
		{
			if (!ToStr.StartsWith(TEXT("N")))
			{
				return false;
			}
			OutLink.ToNodeIndex = FCString::Atoi(*ToStr.Mid(1));
			OutLink.ToInputName.Empty();
		}
	}

	return true;
}

// ---- Graph parsing from lines ----

void FNodeCodeTextFormat::ParseGraphLines(const TArray<FString>& Lines, FNodeCodeGraphIR& OutGraph)
{
	int32 CurrentNodeIndex = -1;

	for (const FString& RawLine : Lines)
	{
		FString Line = TrimLine(RawLine);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		if (Line.StartsWith(TEXT("N")) && !Line.StartsWith(TEXT("N "))
			&& Line.Len() > 1 && FChar::IsDigit(Line[1]))
		{
			FNodeCodeNodeIR Node;
			if (ParseNodeLine(Line, Node))
			{
				CurrentNodeIndex = Node.Index;
				OutGraph.Nodes.Add(MoveTemp(Node));
			}
			continue;
		}

		FString Trimmed = Line;
		if (Trimmed.StartsWith(TEXT(">")))
		{
			if (CurrentNodeIndex >= 0)
			{
				FNodeCodeLinkIR Link;
				if (ParseLinkLine(Trimmed, CurrentNodeIndex, Link))
				{
					OutGraph.Links.Add(MoveTemp(Link));
				}
			}
			continue;
		}
	}
}

void FNodeCodeTextFormat::ParsePropertyLines(const TArray<FString>& Lines, TMap<FString, FString>& OutProperties)
{
	for (const FString& RawLine : Lines)
	{
		FString Line = TrimLine(RawLine);
		if (Line.IsEmpty() || Line.StartsWith(TEXT("#")))
		{
			continue;
		}

		int32 ColonPos;
		if (Line.FindChar(':', ColonPos) && ColonPos > 0)
		{
			FString Key = Line.Left(ColonPos).TrimStartAndEnd();
			FString Value = Line.Mid(ColonPos + 1).TrimStartAndEnd();
			if (!Key.IsEmpty())
			{
				OutProperties.Add(Key, Value);
			}
		}
	}
}

// ---- Serialization ----

FString FNodeCodeTextFormat::GraphToText(const FNodeCodeGraphIR& IR)
{
	FString Result;

	TMap<int32, TArray<const FNodeCodeLinkIR*>> NodeLinks;
	for (const FNodeCodeLinkIR& Link : IR.Links)
	{
		NodeLinks.FindOrAdd(Link.FromNodeIndex).Add(&Link);
	}

	for (const FNodeCodeNodeIR& Node : IR.Nodes)
	{
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

		if (const TArray<const FNodeCodeLinkIR*>* Links = NodeLinks.Find(Node.Index))
		{
			for (const FNodeCodeLinkIR* Link : *Links)
			{
				FString FromPart;
				if (!Link->FromOutputName.IsEmpty())
				{
					FromPart = Link->FromOutputName;
				}

				FString ToPart;
				if (Link->bToGraphOutput)
				{
					ToPart = FString::Printf(TEXT("[%s]"), *Link->ToInputName);
				}
				else if (!Link->ToInputName.IsEmpty())
				{
					ToPart = FString::Printf(TEXT("N%d.%s"), Link->ToNodeIndex, *Link->ToInputName);
				}
				else
				{
					ToPart = FString::Printf(TEXT("N%d"), Link->ToNodeIndex);
				}

				if (FromPart.IsEmpty())
				{
					Result += FString::Printf(TEXT("  > -> %s\n"), *ToPart);
				}
				else
				{
					Result += FString::Printf(TEXT("  > %s -> %s\n"), *FromPart, *ToPart);
				}
			}
		}

		Result += TEXT("\n");
	}

	return Result;
}

FString FNodeCodeTextFormat::PropertiesToText(const TMap<FString, FString>& Properties)
{
	FString Result;
	for (const auto& Pair : Properties)
	{
		Result += FString::Printf(TEXT("%s: %s\n"), *Pair.Key, *Pair.Value);
	}
	return Result;
}

FString FNodeCodeTextFormat::SectionToText(const FNodeCodeSectionIR& Section)
{
	FString Result = Section.GetHeader() + TEXT("\n\n");

	if (Section.IsRawTextSection())
	{
		Result += Section.RawText;
	}
	else if (Section.IsGraphSection())
	{
		Result += GraphToText(Section.Graph);
	}
	else
	{
		Result += PropertiesToText(Section.Properties);
	}

	return Result;
}

FString FNodeCodeTextFormat::DocumentToText(const FNodeCodeDocumentIR& Document)
{
	FString Result;
	for (int32 i = 0; i < Document.Sections.Num(); ++i)
	{
		if (i > 0)
		{
			Result += TEXT("\n");
		}
		Result += SectionToText(Document.Sections[i]);
	}
	return Result;
}

// ---- Document Parsing ----

FNodeCodeSectionIR FNodeCodeTextFormat::ParseSection(const FString& Text, const FString& Type, const FString& Name)
{
	FNodeCodeSectionIR Section;
	Section.Type = Type;
	Section.Name = Name;

	if (Section.IsRawTextSection())
	{
		Section.RawText = Text;
		return Section;
	}

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	if (Section.IsGraphSection())
	{
		ParseGraphLines(Lines, Section.Graph);
	}
	else
	{
		ParsePropertyLines(Lines, Section.Properties);
	}

	return Section;
}

FNodeCodeDocumentIR FNodeCodeTextFormat::ParseDocument(const FString& Text)
{
	FNodeCodeDocumentIR Document;

	TArray<FString> AllLines;
	Text.ParseIntoArrayLines(AllLines);

	FString CurrentType;
	FString CurrentName;
	TArray<FString> CurrentLines;
	bool bHasSection = false;

	auto FlushSection = [&]()
	{
		if (!bHasSection)
		{
			return;
		}

		FNodeCodeSectionIR Section;
		Section.Type = CurrentType;
		Section.Name = CurrentName;

		if (Section.IsRawTextSection())
		{
			Section.RawText = FString::Join(CurrentLines, TEXT("\n"));
			if (!Section.RawText.IsEmpty())
			{
				Section.RawText += TEXT("\n");
			}
		}
		else if (Section.IsGraphSection())
		{
			ParseGraphLines(CurrentLines, Section.Graph);
		}
		else
		{
			ParsePropertyLines(CurrentLines, Section.Properties);
		}

		Document.Sections.Add(MoveTemp(Section));
		CurrentLines.Empty();
	};

	for (const FString& RawLine : AllLines)
	{
		FString Line = TrimLine(RawLine);

		if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
		{
			FlushSection();

			FString Type, Name;
			if (NodeCodeUtils::ParseSectionHeader(Line, Type, Name))
			{
				CurrentType = Type;
				CurrentName = Name;
				bHasSection = true;
			}
			continue;
		}

		if (bHasSection)
		{
			CurrentLines.Add(RawLine);
		}
	}

	FlushSection();

	return Document;
}

// ---- Diff Result ----

FString FNodeCodeTextFormat::DiffResultToJson(const FNodeCodeDiffResult& Result)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

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

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	return OutputString;
}
