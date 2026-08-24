#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ToucanEuwSettingsLibrary.generated.h"

class AActor;

UCLASS(config=EditorPerProjectUserSettings)
class TOUCANCCPICKEREDITOR_API UToucanEuwSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config)
	TMap<FString, FString> EuwSettingsEntries;
};

UCLASS()
class TOUCANCCPICKEREDITOR_API UToucanEuwSettingsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Toucan|EUW Settings")
	static void modifyEuwSettingsEntry(const FString& key, const FString& value);

	UFUNCTION(BlueprintPure, Category="Toucan|EUW Settings")
	static FString getEuwSettingsEntry(const FString& key);

	UFUNCTION(BlueprintCallable, Category="Toucan|Editor")
	static AActor* findFirstActorWithName(const FString& actorName);
};
