// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonObject.h"

class FUCPEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FJsonObject> HandleUndo(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> HandleRedo(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> HandleUndoState(const TSharedPtr<FJsonObject>& Request);
};
