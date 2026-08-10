#pragma once

#include "CoreMinimal.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Rigs/RigHierarchyDefines.h"
#include "ReadControlRigKeyLibrary.generated.h"

class ULevelSequence;
class UControlRig;

USTRUCT(BlueprintType)
struct FControlRigKeyCache
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Toucan|Sequencer")
	TObjectPtr<ULevelSequence> sequence = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Toucan|Sequencer")
	TArray<FRigElementKey> sourceKeys;

	UPROPERTY(BlueprintReadOnly, Category = "Toucan|Sequencer")
	int32 matchedRigCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Toucan|Sequencer")
	int32 matchedKeyCount = 0;

	TMap<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>> keysByRig;
};

UCLASS()
class TOUCANCCPICKEREDITOR_API UReadControlRigKeyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool buildControlRigKeyCache(
		ULevelSequence* sequence,
		const TArray<FRigElementKey>& rigKeys,
		FControlRigKeyCache& outCache
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool updateControlRigKeyCache(
		UPARAM(ref) FControlRigKeyCache& cache,
		ULevelSequence* sequence,
		const TArray<FRigElementKey>& rigKeys
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static void clearControlRigKeyCache(
		UPARAM(ref) FControlRigKeyCache& cache
	);

	UFUNCTION(BlueprintPure, Category = "Toucan|Sequencer")
	static bool isControlRigKeyCacheUsable(const FControlRigKeyCache& cache);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool getControlRotationInSequenceAtFrame(
		const FRigElementKey& rigKey,
		ULevelSequence* sequence,
		int32 frameNumber,
		FRotator& outRotation
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool getControlRotationFromCacheAtFrame(
		const FRigElementKey& rigKey,
		const FControlRigKeyCache& cache,
		int32 frameNumber,
		FRotator& outRotation
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool isControlModifiedInSequenceAtFrame(
		const FRigElementKey& rigKey,
		ULevelSequence* sequence,
		int32 frameNumber,
		FRotator defaultRotation = FRotator::ZeroRotator,
		float tolerance = 0.1f
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool isControlModifiedFromCacheAtFrame(
		const FRigElementKey& rigKey,
		const FControlRigKeyCache& cache,
		int32 frameNumber,
		FRotator defaultRotation = FRotator::ZeroRotator,
		float tolerance = 0.1f
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static void getModifiedControlsInSequenceAtFrame(
		const TArray<FRigElementKey>& rigKeys,
		ULevelSequence* sequence,
		int32 frameNumber,
		float tolerance,
		TArray<FRigElementKey>& outModifiedKeys
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static void getModifiedControlsFromCacheAtFrame(
		const FControlRigKeyCache& cache,
		int32 frameNumber,
		float tolerance,
		TArray<FRigElementKey>& outModifiedKeys
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer", meta = (DisplayName = "Set This Control Current Value", AutoCreateRefTerm = "skipList,animationChannelSkipList"))
	static int32 setThisControlCurrentValue(
		ULevelSequence* sequence,
		const FControlRigSequencerBindingProxy& rigBinding,
		FName ctrlName,
		const TArray<FName>& skipList,
		const TArray<FName>& animationChannelSkipList,
		bool bIncludeAnimationChannels,
		TArray<FRigElementKey>& outKeyedControls,
		float tolerance = 0.01f
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer", meta = (DisplayName = "Set This Control Zero Value", AutoCreateRefTerm = "skipList,animationChannelSkipList"))
	static int32 setThisControlZeroValue(
		ULevelSequence* sequence,
		const FControlRigSequencerBindingProxy& rigBinding,
		FName ctrlName,
		const TArray<FName>& skipList,
		const TArray<FName>& animationChannelSkipList,
		bool bIncludeAnimationChannels,
		TArray<FRigElementKey>& outKeyedControls
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static bool duplicatePreviousControlKeyAtCurrentTime(
		const FControlRigSequencerBindingProxy& rigBinding,
		const FRigElementKey& rigKey
	);

	UFUNCTION(BlueprintCallable, Category = "Toucan|Sequencer")
	static int32 duplicatePreviousControlKeysAtCurrentTime(
		const FControlRigSequencerBindingProxy& rigBinding,
		const TArray<FRigElementKey>& rigKeys,
		TArray<FRigElementKey>& outDuplicatedKeys
	);
};
