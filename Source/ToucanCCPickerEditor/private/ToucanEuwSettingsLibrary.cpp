#include "ToucanEuwSettingsLibrary.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	UWorld* getCurrentEditorWorld()
	{
#if WITH_EDITOR
		if (!GEditor)
		{
			return nullptr;
		}

		for (const FWorldContext& WorldContext : GEditor->GetWorldContexts())
		{
			UWorld* World = WorldContext.World();
			if (World && WorldContext.WorldType == EWorldType::Editor)
			{
				return World;
			}
		}
#endif

		return nullptr;
	}
}

void UToucanEuwSettingsLibrary::modifyEuwSettingsEntry(const FString& key, const FString& value)
{
	if (key.IsEmpty())
	{
		return;
	}

	UToucanEuwSettings* Settings = GetMutableDefault<UToucanEuwSettings>();
	if (!Settings)
	{
		return;
	}

	Settings->EuwSettingsEntries.FindOrAdd(key) = value;
	Settings->SaveConfig(CPF_Config, *GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

FString UToucanEuwSettingsLibrary::getEuwSettingsEntry(const FString& key)
{
	const UToucanEuwSettings* Settings = GetDefault<UToucanEuwSettings>();
	if (!Settings)
	{
		return FString();
	}

	const FString* FoundValue = Settings->EuwSettingsEntries.Find(key);
	return FoundValue ? *FoundValue : FString();
}

AActor* UToucanEuwSettingsLibrary::findFirstActorWithName(const FString& actorName)
{
	FString SearchName = actorName;
	SearchName.TrimStartAndEndInline();

	UWorld* World = getCurrentEditorWorld();
	if (!World || SearchName.IsEmpty())
	{
		return nullptr;
	}

	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		AActor* Actor = *ActorIterator;
		if (Actor && Actor->GetName().Equals(SearchName, ESearchCase::CaseSensitive))
		{
			return Actor;
		}
	}

#if WITH_EDITOR
	for (TActorIterator<AActor> ActorIterator(World); ActorIterator; ++ActorIterator)
	{
		AActor* Actor = *ActorIterator;
		if (Actor && Actor->GetActorLabel().Equals(SearchName, ESearchCase::CaseSensitive))
		{
			return Actor;
		}
	}
#endif

	return nullptr;
}
