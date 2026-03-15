// MIT License - Copyright (c) 2025 Italink

#include "UCPEditorModule.h"
#include "UCPModule.h"
#include "UCPRequestHandler.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Misc/ITransaction.h"

#define LOCTEXT_NAMESPACE "FUCPEditorModule"

void FUCPEditorModule::StartupModule()
{
	if (FUnrealClientProtocolModule::IsAvailable())
	{
		FUCPRequestHandler* Handler = FUnrealClientProtocolModule::Get().GetRequestHandler();
		if (Handler)
		{
			Handler->RegisterCommand(TEXT("undo"), FUCPCommandDelegate::CreateRaw(this, &FUCPEditorModule::HandleUndo));
			Handler->RegisterCommand(TEXT("redo"), FUCPCommandDelegate::CreateRaw(this, &FUCPEditorModule::HandleRedo));
			Handler->RegisterCommand(TEXT("undo_state"), FUCPCommandDelegate::CreateRaw(this, &FUCPEditorModule::HandleUndoState));
		}
	}
}

void FUCPEditorModule::ShutdownModule()
{
	if (FUnrealClientProtocolModule::IsAvailable())
	{
		FUCPRequestHandler* Handler = FUnrealClientProtocolModule::Get().GetRequestHandler();
		if (Handler)
		{
			Handler->UnregisterCommand(TEXT("undo"));
			Handler->UnregisterCommand(TEXT("redo"));
			Handler->UnregisterCommand(TEXT("undo_state"));
		}
	}
}

TSharedPtr<FJsonObject> FUCPEditorModule::HandleUndo(const TSharedPtr<FJsonObject>& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("GEditor not available"));
		return Result;
	}

	bool bSuccess = GEditor->UndoTransaction();
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Nothing to undo"));
	}

	return Result;
}

TSharedPtr<FJsonObject> FUCPEditorModule::HandleRedo(const TSharedPtr<FJsonObject>& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("GEditor not available"));
		return Result;
	}

	bool bSuccess = GEditor->RedoTransaction();
	Result->SetBoolField(TEXT("success"), bSuccess);

	if (!bSuccess)
	{
		Result->SetStringField(TEXT("error"), TEXT("Nothing to redo"));
	}

	return Result;
}

TSharedPtr<FJsonObject> FUCPEditorModule::HandleUndoState(const TSharedPtr<FJsonObject>& Request)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	if (!GEditor || !GEditor->Trans)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("GEditor or transaction system not available"));
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);

	TSharedPtr<FJsonObject> ResultData = MakeShared<FJsonObject>();

	bool bCanUndo = GEditor->Trans->CanUndo();
	bool bCanRedo = GEditor->Trans->CanRedo();
	ResultData->SetBoolField(TEXT("canUndo"), bCanUndo);
	ResultData->SetBoolField(TEXT("canRedo"), bCanRedo);

	if (bCanUndo)
	{
		FTransactionContext UndoCtx = GEditor->Trans->GetUndoContext();
		ResultData->SetStringField(TEXT("undoTitle"), UndoCtx.Title.ToString());
	}
	else
	{
		ResultData->SetStringField(TEXT("undoTitle"), TEXT(""));
	}

	if (bCanRedo)
	{
		FTransactionContext RedoCtx = GEditor->Trans->GetRedoContext();
		ResultData->SetStringField(TEXT("redoTitle"), RedoCtx.Title.ToString());
	}
	else
	{
		ResultData->SetStringField(TEXT("redoTitle"), TEXT(""));
	}

	ResultData->SetNumberField(TEXT("undoCount"), GEditor->Trans->GetUndoCount());
	ResultData->SetNumberField(TEXT("queueLength"), GEditor->Trans->GetQueueLength());

	Result->SetObjectField(TEXT("result"), ResultData);

	return Result;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUCPEditorModule, UnrealClientProtocolEditor)
