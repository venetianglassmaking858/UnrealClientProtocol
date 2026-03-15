// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"

class UClass;

class FMaterialExpressionClassCache
{
public:
	static FMaterialExpressionClassCache& Get();

	void Build();

	UClass* FindClass(const FString& ClassName) const;

	FString GetSerializableName(UClass* InClass) const;

private:
	TMap<FName, UClass*> NameToClass;
	TSet<FName> AmbiguousNames;
	bool bBuilt = false;
};
