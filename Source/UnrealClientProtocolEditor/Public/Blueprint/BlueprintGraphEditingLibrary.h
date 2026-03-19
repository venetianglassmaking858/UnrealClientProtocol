// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlueprintGraphEditingLibrary.generated.h"

UCLASS()
class UBlueprintGraphEditingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UCP|Blueprint")
	static FString ReadGraph(const FString& AssetPath, const FString& ScopeName);

	UFUNCTION(BlueprintCallable, Category = "UCP|Blueprint")
	static FString WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText);

	UFUNCTION(BlueprintCallable, Category = "UCP|Blueprint")
	static FString ListScopes(const FString& AssetPath);
};
