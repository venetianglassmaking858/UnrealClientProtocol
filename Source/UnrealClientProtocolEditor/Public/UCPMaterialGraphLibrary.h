// MIT License - Copyright (c) 2025 Italink

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UCPMaterialGraphLibrary.generated.h"

UCLASS()
class UMaterialGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UCP|MaterialGraph")
	static FString ReadGraph(const FString& AssetPath, const FString& ScopeName);

	UFUNCTION(BlueprintCallable, Category = "UCP|MaterialGraph")
	static FString WriteGraph(const FString& AssetPath, const FString& ScopeName, const FString& GraphText);

	UFUNCTION(BlueprintCallable, Category = "UCP|MaterialGraph")
	static FString Relayout(const FString& AssetPath);

	UFUNCTION(BlueprintCallable, Category = "UCP|MaterialGraph")
	static FString ListScopes(const FString& AssetPath);
};
