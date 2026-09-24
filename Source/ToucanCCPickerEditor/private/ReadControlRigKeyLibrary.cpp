#include "ReadControlRigKeyLibrary.h"

#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "MovieSceneSequencePlayer.h"
#include "ControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Sequencer/ControlRigSequencerHelpers.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "MovieSceneTimeHelpers.h"
#include <initializer_list>

namespace
{
	static void clearCacheData(FControlRigKeyCache& cache)
	{
		cache.sequence = nullptr;
		cache.sourceKeys.Reset();
		cache.matchedRigCount = 0;
		cache.matchedKeyCount = 0;
		cache.keysByRig.Reset();
	}

	static void buildRigKeyMap(
		ULevelSequence* sequence,
		const TArray<FRigElementKey>& rigKeys,
		TMap<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>>& outKeysByRig
	)
	{
		outKeysByRig.Reset();

		if (!IsValid(sequence) || rigKeys.IsEmpty())
		{
			return;
		}

		const TArray<FControlRigSequencerBindingProxy> rigBindings =
			UControlRigSequencerEditorLibrary::GetControlRigs(sequence);

		for (const FControlRigSequencerBindingProxy& binding : rigBindings)
		{
			UControlRig* controlRig = binding.ControlRig;
			if (!IsValid(controlRig))
			{
				continue;
			}

			const URigHierarchy* hierarchy = controlRig->GetHierarchy();
			if (!hierarchy)
			{
				continue;
			}

			TArray<FRigElementKey> matchingKeys;
			matchingKeys.Reserve(rigKeys.Num());

			for (const FRigElementKey& rigKey : rigKeys)
			{
				if (hierarchy->Contains(rigKey))
				{
					matchingKeys.Add(rigKey);
				}
			}

			if (!matchingKeys.IsEmpty())
			{
				outKeysByRig.Add(TWeakObjectPtr<UControlRig>(controlRig), MoveTemp(matchingKeys));
			}
		}
	}

	static bool isRotatorModified(
		const FRotator& currentRotation,
		const FRotator& defaultRotation,
		float tolerance
	)
	{
		return
			!FMath::IsNearlyEqual(currentRotation.Roll, defaultRotation.Roll, tolerance) ||
			!FMath::IsNearlyEqual(currentRotation.Pitch, defaultRotation.Pitch, tolerance) ||
			!FMath::IsNearlyEqual(currentRotation.Yaw, defaultRotation.Yaw, tolerance);
	}

	static bool findRigInCache(
		const FControlRigKeyCache& cache,
		const FRigElementKey& rigKey,
		UControlRig*& outControlRig
	)
	{
		outControlRig = nullptr;

		if (!IsValid(cache.sequence))
		{
			return false;
		}

		for (const TPair<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>>& pair : cache.keysByRig)
		{
			UControlRig* controlRig = pair.Key.Get();
			if (!IsValid(controlRig))
			{
				continue;
			}

			if (pair.Value.Contains(rigKey))
			{
				outControlRig = controlRig;
				return true;
			}
		}

		return false;
	}

	static void fillCacheStats(FControlRigKeyCache& cache)
	{
		cache.matchedRigCount = cache.keysByRig.Num();
		cache.matchedKeyCount = 0;

		for (const TPair<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>>& pair : cache.keysByRig)
		{
			cache.matchedKeyCount += pair.Value.Num();
		}
	}

	static bool evaluateRigAtFrameForRotationScan(
		ULevelSequence* sequence,
		UControlRig* controlRig,
		const TArray<FRigElementKey>& rigKeys,
		FFrameNumber frame
	)
	{
		if (!IsValid(sequence) || !IsValid(controlRig))
		{
			return false;
		}

		// GetLocalControlRigRotator evaluates the entire focused sequence synchronously.
		// Do that once for the rig, then read every control from the evaluated hierarchy.
		for (const FRigElementKey& rigKey : rigKeys)
		{
			const FRigControlElement* control = controlRig->FindControl(rigKey.Name);
			if (control && control->Settings.ControlType == ERigControlType::Rotator)
			{
				UControlRigSequencerEditorLibrary::GetLocalControlRigRotator(
					sequence,
					controlRig,
					rigKey.Name,
					frame,
					EMovieSceneTimeUnit::DisplayRate
				);
				return true;
			}
		}

		return false;
	}

	static void appendModifiedRotatorControls(
		UControlRig* controlRig,
		const TArray<FRigElementKey>& rigKeys,
		float tolerance,
		TArray<FRigElementKey>& outModifiedKeys
	)
	{
		for (const FRigElementKey& rigKey : rigKeys)
		{
			const FRigControlElement* control = controlRig->FindControl(rigKey.Name);
			if (!control || control->Settings.ControlType != ERigControlType::Rotator)
			{
				continue;
			}

			const FRigControlValue rigValue = controlRig->GetControlValue(rigKey.Name);
			const FRotator currentRotation =
				FRotator::MakeFromEuler((FVector)rigValue.Get<FVector3f>());

			if (isRotatorModified(currentRotation, FRotator::ZeroRotator, tolerance))
			{
				outModifiedKeys.Add(rigKey);
			}
		}
	}

	static bool isRigStillBoundToSequence(ULevelSequence* sequence, UControlRig* controlRig)
	{
		if (!IsValid(sequence) || !IsValid(controlRig))
		{
			return false;
		}

		const TArray<FControlRigSequencerBindingProxy> rigBindings =
			UControlRigSequencerEditorLibrary::GetControlRigs(sequence);

		for (const FControlRigSequencerBindingProxy& binding : rigBindings)
		{
			if (binding.ControlRig == controlRig)
			{
				return true;
			}
		}

		return false;
	}

	static FFrameNumber getCurrentSequencerFrame()
	{
		const FMovieSceneSequencePlaybackParams position =
			ULevelSequenceEditorBlueprintLibrary::GetGlobalPosition(EMovieSceneTimeUnit::DisplayRate);

		return position.Frame.GetFrame();
	}

	static UMovieSceneControlRigParameterSection* findSectionForControl(
		UMovieSceneControlRigParameterTrack* track,
		FName controlName,
		FFrameNumber currentFrame
	)
	{
		if (!track)
		{
			return nullptr;
		}

		if (UMovieSceneControlRigParameterSection* sectionToKey =
			Cast<UMovieSceneControlRigParameterSection>(track->GetSectionToKey(controlName)))
		{
			return sectionToKey;
		}

		for (UMovieSceneSection* movieSection : track->GetAllSections())
		{
			UMovieSceneControlRigParameterSection* section =
				Cast<UMovieSceneControlRigParameterSection>(movieSection);
			if (section && section->IsActive() && section->GetRange().Contains(currentFrame))
			{
				return section;
			}
		}

		for (UMovieSceneSection* movieSection : track->GetAllSections())
		{
			UMovieSceneControlRigParameterSection* section =
				Cast<UMovieSceneControlRigParameterSection>(movieSection);
			if (section && section->IsActive())
			{
				return section;
			}
		}

		return nullptr;
	}

	template<typename ValueType>
	static bool findPreviousKeyIndex(
		TArrayView<const FFrameNumber> keyTimes,
		TArrayView<const ValueType> keyValues,
		FFrameNumber currentFrame,
		int32& outPreviousIndex
	)
	{
		outPreviousIndex = INDEX_NONE;

		const int32 keyCount = FMath::Min(keyTimes.Num(), keyValues.Num());
		for (int32 keyIndex = 0; keyIndex < keyCount; ++keyIndex)
		{
			if (keyTimes[keyIndex] < currentFrame)
			{
				outPreviousIndex = keyIndex;
			}
			else
			{
				break;
			}
		}

		return outPreviousIndex != INDEX_NONE;
	}

	static bool duplicatePreviousFloatChannelKey(
		FMovieSceneFloatChannel* channel,
		FFrameNumber currentFrame
	)
	{
		if (!channel)
		{
			return false;
		}

		int32 previousIndex = INDEX_NONE;
		if (!findPreviousKeyIndex(channel->GetTimes(), channel->GetValues(), currentFrame, previousIndex))
		{
			return false;
		}

		const FMovieSceneFloatValue previousValue = channel->GetValues()[previousIndex];
		channel->GetData().UpdateOrAddKey(currentFrame, previousValue);
		return true;
	}

	static bool duplicatePreviousBoolChannelKey(
		FMovieSceneBoolChannel* channel,
		FFrameNumber currentFrame
	)
	{
		if (!channel)
		{
			return false;
		}

		int32 previousIndex = INDEX_NONE;
		if (!findPreviousKeyIndex(channel->GetTimes(), channel->GetValues(), currentFrame, previousIndex))
		{
			return false;
		}

		const bool previousValue = channel->GetValues()[previousIndex];
		channel->GetData().UpdateOrAddKey(currentFrame, previousValue);
		return true;
	}

	static bool duplicatePreviousIntegerChannelKey(
		FMovieSceneIntegerChannel* channel,
		FFrameNumber currentFrame
	)
	{
		if (!channel)
		{
			return false;
		}

		int32 previousIndex = INDEX_NONE;
		if (!findPreviousKeyIndex(channel->GetTimes(), channel->GetValues(), currentFrame, previousIndex))
		{
			return false;
		}

		const int32 previousValue = channel->GetValues()[previousIndex];
		channel->GetData().UpdateOrAddKey(currentFrame, previousValue);
		return true;
	}

	static bool duplicatePreviousByteChannelKey(
		FMovieSceneByteChannel* channel,
		FFrameNumber currentFrame
	)
	{
		if (!channel)
		{
			return false;
		}

		int32 previousIndex = INDEX_NONE;
		if (!findPreviousKeyIndex(channel->GetTimes(), channel->GetValues(), currentFrame, previousIndex))
		{
			return false;
		}

		const uint8 previousValue = channel->GetValues()[previousIndex];
		channel->GetData().UpdateOrAddKey(currentFrame, previousValue);
		return true;
	}

	static bool duplicatePreviousControlElementChannels(
		const UControlRig* controlRig,
		const FRigControlElement* controlElement,
		UMovieSceneControlRigParameterSection* section,
		FFrameNumber currentFrame
	)
	{
		if (!IsValid(controlRig) || !controlElement || !section)
		{
			return false;
		}

		const FName controlName = controlElement->GetKey().Name;
		bool duplicatedAnyChannel = false;

		switch (controlElement->Settings.ControlType)
		{
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
			case ERigControlType::Vector2D:
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
			{
				TArrayView<FMovieSceneFloatChannel*> channels =
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section);
				for (FMovieSceneFloatChannel* channel : channels)
				{
					duplicatedAnyChannel |= duplicatePreviousFloatChannelKey(channel, currentFrame);
				}
				break;
			}
			case ERigControlType::Bool:
			{
				TArrayView<FMovieSceneBoolChannel*> channels =
					FControlRigSequencerHelpers::GetBoolChannels(controlRig, controlName, section);
				for (FMovieSceneBoolChannel* channel : channels)
				{
					duplicatedAnyChannel |= duplicatePreviousBoolChannelKey(channel, currentFrame);
				}
				break;
			}
			case ERigControlType::Integer:
			{
				TArrayView<FMovieSceneIntegerChannel*> channels =
					FControlRigSequencerHelpers::GetIntegerChannels(controlRig, controlName, section);
				for (FMovieSceneIntegerChannel* channel : channels)
				{
					duplicatedAnyChannel |= duplicatePreviousIntegerChannelKey(channel, currentFrame);
				}
				break;
			}
			default:
			{
				TArrayView<FMovieSceneByteChannel*> channels =
					FControlRigSequencerHelpers::GetByteChannels(controlRig, controlName, section);
				for (FMovieSceneByteChannel* channel : channels)
				{
					duplicatedAnyChannel |= duplicatePreviousByteChannelKey(channel, currentFrame);
				}
				break;
			}
		}

		return duplicatedAnyChannel;
	}

	static bool isVectorNearlyEqual(const FVector& value, const FVector& defaultValue, float tolerance)
	{
		return
			FMath::IsNearlyEqual(value.X, defaultValue.X, tolerance) &&
			FMath::IsNearlyEqual(value.Y, defaultValue.Y, tolerance) &&
			FMath::IsNearlyEqual(value.Z, defaultValue.Z, tolerance);
	}

	static bool isVector2DNearlyEqual(const FVector2D& value, const FVector2D& defaultValue, float tolerance)
	{
		return
			FMath::IsNearlyEqual(value.X, defaultValue.X, tolerance) &&
			FMath::IsNearlyEqual(value.Y, defaultValue.Y, tolerance);
	}

	static bool isRotatorNearlyEqual(const FRotator& value, const FRotator& defaultValue, float tolerance)
	{
		return
			FMath::IsNearlyEqual(value.Roll, defaultValue.Roll, tolerance) &&
			FMath::IsNearlyEqual(value.Pitch, defaultValue.Pitch, tolerance) &&
			FMath::IsNearlyEqual(value.Yaw, defaultValue.Yaw, tolerance);
	}

	static bool isTransformNearlyIdentity(const FTransform& value, float tolerance)
	{
		return
			isVectorNearlyEqual(value.GetLocation(), FVector::ZeroVector, tolerance) &&
			isRotatorNearlyEqual(value.Rotator(), FRotator::ZeroRotator, tolerance) &&
			isVectorNearlyEqual(value.GetScale3D(), FVector::OneVector, tolerance);
	}

	static bool isTransformNoScaleNearlyIdentity(const FTransformNoScale& value, float tolerance)
	{
		return
			isVectorNearlyEqual(value.Location, FVector::ZeroVector, tolerance) &&
			isRotatorNearlyEqual(value.Rotation.Rotator(), FRotator::ZeroRotator, tolerance);
	}

	static bool isEulerTransformNearlyIdentity(const FEulerTransform& value, float tolerance)
	{
		return
			isVectorNearlyEqual(value.Location, FVector::ZeroVector, tolerance) &&
			isRotatorNearlyEqual(value.Rotation, FRotator::ZeroRotator, tolerance) &&
			isVectorNearlyEqual(value.Scale, FVector::OneVector, tolerance);
	}

	static bool setControlCurrentValueIfModified(
		ULevelSequence* sequence,
		UControlRig* controlRig,
		const FRigControlElement* controlElement,
		FFrameNumber currentFrame,
		float tolerance
	)
	{
		if (!IsValid(sequence) || !IsValid(controlRig) || !controlElement)
		{
			return false;
		}

		const FName controlName = controlElement->GetKey().Name;
		constexpr EMovieSceneTimeUnit timeUnit = EMovieSceneTimeUnit::DisplayRate;

		switch (controlElement->Settings.ControlType)
		{
			case ERigControlType::Bool:
			{
				const bool value = UControlRigSequencerEditorLibrary::GetLocalControlRigBool(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (!value)
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigBool(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Float:
			{
				const float value = UControlRigSequencerEditorLibrary::GetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (FMath::IsNearlyZero(value, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::ScaleFloat:
			{
				const float value = UControlRigSequencerEditorLibrary::GetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (FMath::IsNearlyEqual(value, 1.0f, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Integer:
			{
				const int32 value = UControlRigSequencerEditorLibrary::GetLocalControlRigInt(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (value == 0)
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigInt(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Vector2D:
			{
				const FVector2D value = UControlRigSequencerEditorLibrary::GetLocalControlRigVector2D(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isVector2DNearlyEqual(value, FVector2D::ZeroVector, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigVector2D(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Position:
			{
				const FVector value = UControlRigSequencerEditorLibrary::GetLocalControlRigPosition(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isVectorNearlyEqual(value, FVector::ZeroVector, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigPosition(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Scale:
			{
				const FVector value = UControlRigSequencerEditorLibrary::GetLocalControlRigScale(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isVectorNearlyEqual(value, FVector::OneVector, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigScale(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Rotator:
			{
				const FRotator value = UControlRigSequencerEditorLibrary::GetLocalControlRigRotator(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isRotatorNearlyEqual(value, FRotator::ZeroRotator, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigRotator(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::EulerTransform:
			{
				const FEulerTransform value = UControlRigSequencerEditorLibrary::GetLocalControlRigEulerTransform(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isEulerTransformNearlyIdentity(value, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigEulerTransform(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::TransformNoScale:
			{
				const FTransformNoScale value = UControlRigSequencerEditorLibrary::GetLocalControlRigTransformNoScale(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isTransformNoScaleNearlyIdentity(value, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigTransformNoScale(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			case ERigControlType::Transform:
			{
				const FTransform value = UControlRigSequencerEditorLibrary::GetLocalControlRigTransform(
					sequence, controlRig, controlName, currentFrame, timeUnit);
				if (isTransformNearlyIdentity(value, tolerance))
				{
					return false;
				}

				UControlRigSequencerEditorLibrary::SetLocalControlRigTransform(
					sequence, controlRig, controlName, currentFrame, value, timeUnit, true);
				return true;
			}
			default:
				return false;
		}
	}

	static bool setControlZeroValue(
		ULevelSequence* sequence,
		UControlRig* controlRig,
		const FRigControlElement* controlElement,
		FFrameNumber currentFrame
	)
	{
		if (!IsValid(sequence) || !IsValid(controlRig) || !controlElement)
		{
			return false;
		}

		const FName controlName = controlElement->GetKey().Name;
		constexpr EMovieSceneTimeUnit timeUnit = EMovieSceneTimeUnit::DisplayRate;

		switch (controlElement->Settings.ControlType)
		{
			case ERigControlType::Bool:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigBool(
					sequence, controlRig, controlName, currentFrame, false, timeUnit, true);
				return true;
			}
			case ERigControlType::Float:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, 0.0f, timeUnit, true);
				return true;
			}
			case ERigControlType::ScaleFloat:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(
					sequence, controlRig, controlName, currentFrame, 1.0f, timeUnit, true);
				return true;
			}
			case ERigControlType::Integer:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigInt(
					sequence, controlRig, controlName, currentFrame, 0, timeUnit, true);
				return true;
			}
			case ERigControlType::Vector2D:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigVector2D(
					sequence, controlRig, controlName, currentFrame, FVector2D::ZeroVector, timeUnit, true);
				return true;
			}
			case ERigControlType::Position:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigPosition(
					sequence, controlRig, controlName, currentFrame, FVector::ZeroVector, timeUnit, true);
				return true;
			}
			case ERigControlType::Scale:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigScale(
					sequence, controlRig, controlName, currentFrame, FVector::OneVector, timeUnit, true);
				return true;
			}
			case ERigControlType::Rotator:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigRotator(
					sequence, controlRig, controlName, currentFrame, FRotator::ZeroRotator, timeUnit, true);
				return true;
			}
			case ERigControlType::EulerTransform:
			{
				FEulerTransform zeroValue;
				zeroValue.Location = FVector::ZeroVector;
				zeroValue.Rotation = FRotator::ZeroRotator;
				zeroValue.Scale = FVector::OneVector;

				UControlRigSequencerEditorLibrary::SetLocalControlRigEulerTransform(
					sequence, controlRig, controlName, currentFrame, zeroValue, timeUnit, true);
				return true;
			}
			case ERigControlType::TransformNoScale:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigTransformNoScale(
					sequence, controlRig, controlName, currentFrame, FTransformNoScale(), timeUnit, true);
				return true;
			}
			case ERigControlType::Transform:
			{
				UControlRigSequencerEditorLibrary::SetLocalControlRigTransform(
					sequence, controlRig, controlName, currentFrame, FTransform::Identity, timeUnit, true);
				return true;
			}
			default:
				return false;
		}
	}

	static bool updateFloatChannelsAtFrame(
		TArrayView<FMovieSceneFloatChannel*> channels,
		FFrameNumber currentFrame,
		std::initializer_list<float> values
	)
	{
		if (channels.Num() != static_cast<int32>(values.size()))
		{
			return false;
		}

		int32 valueIndex = 0;
		for (const float value : values)
		{
			FMovieSceneFloatChannel* channel = channels[valueIndex++];
			if (!channel)
			{
				return false;
			}

			channel->GetData().UpdateOrAddKey(currentFrame, FMovieSceneFloatValue(value));
		}

		return true;
	}

	static bool updateBoolChannelAtFrame(
		TArrayView<FMovieSceneBoolChannel*> channels,
		FFrameNumber currentFrame,
		bool value
	)
	{
		if (channels.Num() != 1 || !channels[0])
		{
			return false;
		}

		channels[0]->GetData().UpdateOrAddKey(currentFrame, value);
		return true;
	}

	static bool updateIntegerChannelAtFrame(
		TArrayView<FMovieSceneIntegerChannel*> channels,
		FFrameNumber currentFrame,
		int32 value
	)
	{
		if (channels.Num() != 1 || !channels[0])
		{
			return false;
		}

		channels[0]->GetData().UpdateOrAddKey(currentFrame, value);
		return true;
	}

	static bool updateByteChannelAtFrame(
		TArrayView<FMovieSceneByteChannel*> channels,
		FFrameNumber currentFrame,
		uint8 value
	)
	{
		if (channels.Num() != 1 || !channels[0])
		{
			return false;
		}

		channels[0]->GetData().UpdateOrAddKey(currentFrame, value);
		return true;
	}

	static bool setControlCurrentValueDirectIfModified(
		UControlRig* controlRig,
		UMovieSceneControlRigParameterTrack* track,
		const FRigControlElement* controlElement,
		FFrameNumber currentFrame,
		float tolerance
	)
	{
		if (!IsValid(controlRig) || !IsValid(track) || !controlElement)
		{
			return false;
		}

		const FName controlName = controlElement->GetKey().Name;
		UMovieSceneControlRigParameterSection* section = findSectionForControl(track, controlName, currentFrame);
		if (!section)
		{
			return false;
		}

		FRigControlElement* mutableControlElement = const_cast<FRigControlElement*>(controlElement);
		section->Modify();

		switch (controlElement->Settings.ControlType)
		{
			case ERigControlType::Bool:
			{
				const bool value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<bool>();
				if (!value)
				{
					return false;
				}

				return updateBoolChannelAtFrame(
					FControlRigSequencerHelpers::GetBoolChannels(controlRig, controlName, section),
					currentFrame,
					value);
			}
			case ERigControlType::Float:
			{
				const float value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<float>();
				if (FMath::IsNearlyZero(value, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ value });
			}
			case ERigControlType::ScaleFloat:
			{
				const float value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<float>();
				if (FMath::IsNearlyEqual(value, 1.0f, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ value });
			}
			case ERigControlType::Integer:
			{
				if (controlElement->Settings.ControlEnum != nullptr)
				{
					const uint8 value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<uint8>();
					if (value == 0)
					{
						return false;
					}

					return updateByteChannelAtFrame(
						FControlRigSequencerHelpers::GetByteChannels(controlRig, controlName, section),
						currentFrame,
						value);
				}

				const int32 value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<int32>();
				if (value == 0)
				{
					return false;
				}

				return updateIntegerChannelAtFrame(
					FControlRigSequencerHelpers::GetIntegerChannels(controlRig, controlName, section),
					currentFrame,
					value);
			}
			case ERigControlType::Vector2D:
			{
				const FVector3f rawValue = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FVector3f>();
				const FVector2D value(rawValue.X, rawValue.Y);
				if (isVector2DNearlyEqual(value, FVector2D::ZeroVector, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(value.X), static_cast<float>(value.Y) });
			}
			case ERigControlType::Position:
			{
				const FVector value = (FVector)controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FVector3f>();
				if (isVectorNearlyEqual(value, FVector::ZeroVector, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(value.X), static_cast<float>(value.Y), static_cast<float>(value.Z) });
			}
			case ERigControlType::Scale:
			{
				const FVector value = (FVector)controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FVector3f>();
				if (isVectorNearlyEqual(value, FVector::OneVector, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(value.X), static_cast<float>(value.Y), static_cast<float>(value.Z) });
			}
			case ERigControlType::Rotator:
			{
				const FVector euler = controlRig->GetControlSpecifiedEulerAngle(mutableControlElement);
				const FRotator value(euler.Y, euler.Z, euler.X);
				if (isRotatorNearlyEqual(value, FRotator::ZeroRotator, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(value.Roll), static_cast<float>(value.Pitch), static_cast<float>(value.Yaw) });
			}
			case ERigControlType::Transform:
			{
				const FTransform value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransform_Float>().ToTransform();
				FEulerTransform eulerTransform(value);
				const FVector euler = controlRig->GetControlSpecifiedEulerAngle(mutableControlElement);
				eulerTransform.Rotation = FRotator(euler.Y, euler.Z, euler.X);
				if (isEulerTransformNearlyIdentity(eulerTransform, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(eulerTransform.Location.X), static_cast<float>(eulerTransform.Location.Y), static_cast<float>(eulerTransform.Location.Z), static_cast<float>(eulerTransform.Rotation.Roll), static_cast<float>(eulerTransform.Rotation.Pitch), static_cast<float>(eulerTransform.Rotation.Yaw), static_cast<float>(eulerTransform.Scale.X), static_cast<float>(eulerTransform.Scale.Y), static_cast<float>(eulerTransform.Scale.Z) });
			}
			case ERigControlType::TransformNoScale:
			{
				const FTransformNoScale value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FRigControlValue::FTransformNoScale_Float>().ToTransform();
				FEulerTransform eulerTransform(value.ToFTransform());
				const FVector euler = controlRig->GetControlSpecifiedEulerAngle(mutableControlElement);
				eulerTransform.Rotation = FRotator(euler.Y, euler.Z, euler.X);
				if (isTransformNoScaleNearlyIdentity(value, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(eulerTransform.Location.X), static_cast<float>(eulerTransform.Location.Y), static_cast<float>(eulerTransform.Location.Z), static_cast<float>(eulerTransform.Rotation.Roll), static_cast<float>(eulerTransform.Rotation.Pitch), static_cast<float>(eulerTransform.Rotation.Yaw) });
			}
			case ERigControlType::EulerTransform:
			{
				FEulerTransform value = controlRig->GetControlValue(mutableControlElement, ERigControlValueType::Current).Get<FRigControlValue::FEulerTransform_Float>().ToTransform();
				const FVector euler = controlRig->GetControlSpecifiedEulerAngle(mutableControlElement);
				value.Rotation = FRotator(euler.Y, euler.Z, euler.X);
				if (isEulerTransformNearlyIdentity(value, tolerance))
				{
					return false;
				}

				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ static_cast<float>(value.Location.X), static_cast<float>(value.Location.Y), static_cast<float>(value.Location.Z), static_cast<float>(value.Rotation.Roll), static_cast<float>(value.Rotation.Pitch), static_cast<float>(value.Rotation.Yaw), static_cast<float>(value.Scale.X), static_cast<float>(value.Scale.Y), static_cast<float>(value.Scale.Z) });
			}
			default:
				return false;
		}
	}
	static bool setControlZeroValueDirect(
		UControlRig* controlRig,
		UMovieSceneControlRigParameterTrack* track,
		const FRigControlElement* controlElement,
		FFrameNumber currentFrame
	)
	{
		if (!IsValid(controlRig) || !IsValid(track) || !controlElement)
		{
			return false;
		}

		const FName controlName = controlElement->GetKey().Name;
		UMovieSceneControlRigParameterSection* section = findSectionForControl(track, controlName, currentFrame);
		if (!section)
		{
			return false;
		}

		section->Modify();

		switch (controlElement->Settings.ControlType)
		{
			case ERigControlType::Bool:
			{
				TArrayView<FMovieSceneBoolChannel*> channels =
					FControlRigSequencerHelpers::GetBoolChannels(controlRig, controlName, section);
				if (channels.Num() != 1 || !channels[0])
				{
					return false;
				}

				channels[0]->GetData().UpdateOrAddKey(currentFrame, false);
				return true;
			}
			case ERigControlType::Float:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 0.0f });
			}
			case ERigControlType::ScaleFloat:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 1.0f });
			}
			case ERigControlType::Integer:
			{
				TArrayView<FMovieSceneIntegerChannel*> integerChannels =
					FControlRigSequencerHelpers::GetIntegerChannels(controlRig, controlName, section);
				if (integerChannels.Num() == 1 && integerChannels[0])
				{
					integerChannels[0]->GetData().UpdateOrAddKey(currentFrame, 0);
					return true;
				}

				TArrayView<FMovieSceneByteChannel*> byteChannels =
					FControlRigSequencerHelpers::GetByteChannels(controlRig, controlName, section);
				if (byteChannels.Num() == 1 && byteChannels[0])
				{
					byteChannels[0]->GetData().UpdateOrAddKey(currentFrame, 0);
					return true;
				}

				return false;
			}
			case ERigControlType::Vector2D:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 0.0f, 0.0f });
			}
			case ERigControlType::Position:
			case ERigControlType::Rotator:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 0.0f, 0.0f, 0.0f });
			}
			case ERigControlType::Scale:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 1.0f, 1.0f, 1.0f });
			}
			case ERigControlType::TransformNoScale:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f });
			}
			case ERigControlType::Transform:
			case ERigControlType::EulerTransform:
			{
				return updateFloatChannelsAtFrame(
					FControlRigSequencerHelpers::GetFloatChannels(controlRig, controlName, section),
					currentFrame,
					{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f });
			}
			default:
				return false;
		}
	}
	static bool isAnimationChannelSkipped(
		FName parentControlName,
		FName animationChannelName,
		const TArray<FName>& animationChannelSkipList
	)
	{
		if (animationChannelSkipList.Contains(animationChannelName))
		{
			return true;
		}

		const FName qualifiedChannelName(*FString::Printf(
			TEXT("%s.%s"),
			*parentControlName.ToString(),
			*animationChannelName.ToString()
		));
		return animationChannelSkipList.Contains(qualifiedChannelName);
	}

	static bool isAnimationControlElementSkipped(
		const URigHierarchy* hierarchy,
		const FRigElementKey& controlKey,
		const FRigControlElement* controlElement,
		const TArray<FName>& animationChannelSkipList
	)
	{
		if (!hierarchy || !controlElement || !controlElement->IsAnimationChannel())
		{
			return false;
		}

		if (animationChannelSkipList.Contains(controlKey.Name))
		{
			return true;
		}

		const FRigElementKey parentKey = hierarchy->GetFirstParent(controlKey);
		if (parentKey.IsValid())
		{
			return isAnimationChannelSkipped(parentKey.Name, controlKey.Name, animationChannelSkipList);
		}

		return false;
	}

	static bool isControlSkippedByNameOrQualifiedName(
		const URigHierarchy* hierarchy,
		const FRigElementKey& controlKey,
		const TArray<FName>& skipList
	)
	{
		if (skipList.Contains(controlKey.Name))
		{
			return true;
		}

		if (!hierarchy)
		{
			return false;
		}

		const FRigElementKey parentKey = hierarchy->GetFirstParent(controlKey);
		if (!parentKey.IsValid())
		{
			return false;
		}

		const FName qualifiedControlName(*FString::Printf(
			TEXT("%s.%s"),
			*parentKey.Name.ToString(),
			*controlKey.Name.ToString()
		));
		return skipList.Contains(qualifiedControlName);
	}
}

bool UReadControlRigKeyLibrary::buildControlRigKeyCache(
	ULevelSequence* sequence,
	const TArray<FRigElementKey>& rigKeys,
	FControlRigKeyCache& outCache
)
{
	clearCacheData(outCache);

	if (!IsValid(sequence) || rigKeys.IsEmpty())
	{
		return false;
	}

	outCache.sequence = sequence;
	outCache.sourceKeys = rigKeys;
	buildRigKeyMap(sequence, rigKeys, outCache.keysByRig);
	fillCacheStats(outCache);

	return outCache.matchedRigCount > 0;
}

bool UReadControlRigKeyLibrary::updateControlRigKeyCache(
	FControlRigKeyCache& cache,
	ULevelSequence* sequence,
	const TArray<FRigElementKey>& rigKeys
)
{
	return buildControlRigKeyCache(sequence, rigKeys, cache);
}

void UReadControlRigKeyLibrary::clearControlRigKeyCache(FControlRigKeyCache& cache)
{
	clearCacheData(cache);
}

bool UReadControlRigKeyLibrary::isControlRigKeyCacheUsable(const FControlRigKeyCache& cache)
{
	return IsValid(cache.sequence) && cache.keysByRig.Num() > 0;
}

bool UReadControlRigKeyLibrary::getControlRotationInSequenceAtFrame(
	const FRigElementKey& rigKey,
	ULevelSequence* sequence,
	int32 frameNumber,
	FRotator& outRotation
)
{
	outRotation = FRotator::ZeroRotator;

	if (!IsValid(sequence))
	{
		return false;
	}

	const TArray<FControlRigSequencerBindingProxy> rigBindings =
		UControlRigSequencerEditorLibrary::GetControlRigs(sequence);

	UControlRig* matchedRig = nullptr;

	for (const FControlRigSequencerBindingProxy& binding : rigBindings)
	{
		UControlRig* controlRig = binding.ControlRig;
		if (!IsValid(controlRig))
		{
			continue;
		}

		const URigHierarchy* hierarchy = controlRig->GetHierarchy();
		if (!hierarchy)
		{
			continue;
		}

		if (hierarchy->Contains(rigKey))
		{
			matchedRig = controlRig;
			break;
		}
	}

	if (!IsValid(matchedRig))
	{
		return false;
	}

	outRotation = UControlRigSequencerEditorLibrary::GetLocalControlRigRotator(
		sequence,
		matchedRig,
		rigKey.Name,
		FFrameNumber(frameNumber),
		EMovieSceneTimeUnit::DisplayRate
	);

	return true;
}

bool UReadControlRigKeyLibrary::getControlRotationFromCacheAtFrame(
	const FRigElementKey& rigKey,
	const FControlRigKeyCache& cache,
	int32 frameNumber,
	FRotator& outRotation
)
{
	outRotation = FRotator::ZeroRotator;

	UControlRig* matchedRig = nullptr;
	if (!findRigInCache(cache, rigKey, matchedRig) || !IsValid(matchedRig) || !IsValid(cache.sequence))
	{
		return false;
	}

	if (!isRigStillBoundToSequence(cache.sequence, matchedRig))
	{
		return false;
	}

	outRotation = UControlRigSequencerEditorLibrary::GetLocalControlRigRotator(
		cache.sequence,
		matchedRig,
		rigKey.Name,
		FFrameNumber(frameNumber),
		EMovieSceneTimeUnit::DisplayRate
	);

	return true;
}

bool UReadControlRigKeyLibrary::isControlModifiedInSequenceAtFrame(
	const FRigElementKey& rigKey,
	ULevelSequence* sequence,
	int32 frameNumber,
	FRotator defaultRotation,
	float tolerance
)
{
	FRotator currentRotation;
	const bool gotRotation = getControlRotationInSequenceAtFrame(
		rigKey,
		sequence,
		frameNumber,
		currentRotation
	);

	if (!gotRotation)
	{
		return false;
	}

	return isRotatorModified(currentRotation, defaultRotation, tolerance);
}

bool UReadControlRigKeyLibrary::isControlModifiedFromCacheAtFrame(
	const FRigElementKey& rigKey,
	const FControlRigKeyCache& cache,
	int32 frameNumber,
	FRotator defaultRotation,
	float tolerance
)
{
	FRotator currentRotation;
	const bool gotRotation = getControlRotationFromCacheAtFrame(
		rigKey,
		cache,
		frameNumber,
		currentRotation
	);

	if (!gotRotation)
	{
		return false;
	}

	return isRotatorModified(currentRotation, defaultRotation, tolerance);
}

void UReadControlRigKeyLibrary::getModifiedControlsInSequenceAtFrame(
	const TArray<FRigElementKey>& rigKeys,
	ULevelSequence* sequence,
	int32 frameNumber,
	float tolerance,
	TArray<FRigElementKey>& outModifiedKeys
)
{
	outModifiedKeys.Reset();

	if (!IsValid(sequence) || rigKeys.IsEmpty())
	{
		return;
	}

	TMap<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>> keysByRig;
	buildRigKeyMap(sequence, rigKeys, keysByRig);

	const FFrameNumber discreteFrame(frameNumber);

	for (const TPair<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>>& pair : keysByRig)
	{
		UControlRig* controlRig = pair.Key.Get();
		if (!IsValid(controlRig) || !controlRig->GetHierarchy())
		{
			continue;
		}

		if (evaluateRigAtFrameForRotationScan(sequence, controlRig, pair.Value, discreteFrame))
		{
			appendModifiedRotatorControls(controlRig, pair.Value, tolerance, outModifiedKeys);
		}
	}
}

void UReadControlRigKeyLibrary::getModifiedControlsFromCacheAtFrame(
	const FControlRigKeyCache& cache,
	int32 frameNumber,
	float tolerance,
	TArray<FRigElementKey>& outModifiedKeys
)
{
	outModifiedKeys.Reset();

	if (!IsValid(cache.sequence) || cache.keysByRig.IsEmpty())
	{
		return;
	}

	const FFrameNumber discreteFrame(frameNumber);

	for (const TPair<TWeakObjectPtr<UControlRig>, TArray<FRigElementKey>>& pair : cache.keysByRig)
	{
		UControlRig* controlRig = pair.Key.Get();
		if (!IsValid(controlRig) || !controlRig->GetHierarchy())
		{
			continue;
		}

		if (!isRigStillBoundToSequence(cache.sequence, controlRig))
		{
			continue;
		}

		if (evaluateRigAtFrameForRotationScan(cache.sequence, controlRig, pair.Value, discreteFrame))
		{
			appendModifiedRotatorControls(controlRig, pair.Value, tolerance, outModifiedKeys);
		}
	}
}

int32 UReadControlRigKeyLibrary::setThisControlCurrentValue(
	ULevelSequence* sequence,
	const FControlRigSequencerBindingProxy& rigBinding,
	FName ctrlName,
	const TArray<FName>& skipList,
	const TArray<FName>& animationChannelSkipList,
	bool bIncludeAnimationChannels,
	TArray<FRigElementKey>& outKeyedControls,
	float tolerance
)
{
	outKeyedControls.Reset();

	UControlRig* controlRig = rigBinding.ControlRig;
	if (!IsValid(sequence) || !IsValid(controlRig) || ctrlName.IsNone() || skipList.Contains(ctrlName))
	{
		return 0;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return 0;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	const FRigElementKey controlKey(ctrlName, ERigElementType::Control);
	const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
	if (!controlElement || !hierarchy->IsAnimatable(controlElement))
	{
		return 0;
	}

	if (
		isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, skipList) ||
		isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, animationChannelSkipList) ||
		isAnimationControlElementSkipped(hierarchy, controlKey, controlElement, animationChannelSkipList)
	)
	{
		return 0;
	}

	if (setControlCurrentValueIfModified(sequence, controlRig, controlElement, currentFrame, tolerance))
	{
		outKeyedControls.Add(controlKey);
	}

	if (bIncludeAnimationChannels)
	{
		for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
		{
			if (
				childKey.Type != ERigElementType::Control ||
				isControlSkippedByNameOrQualifiedName(hierarchy, childKey, skipList) ||
				isAnimationChannelSkipped(controlKey.Name, childKey.Name, animationChannelSkipList)
			)
			{
				continue;
			}

			const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
			if (!childControlElement || !childControlElement->IsAnimationChannel() || !hierarchy->IsAnimatable(childControlElement))
			{
				continue;
			}

			if (setControlCurrentValueIfModified(sequence, controlRig, childControlElement, currentFrame, tolerance))
			{
				outKeyedControls.Add(childKey);
			}
		}
	}

	return outKeyedControls.Num();
}

int32 UReadControlRigKeyLibrary::setControlCurrentValues(
	ULevelSequence* sequence,
	const FControlRigSequencerBindingProxy& rigBinding,
	const TArray<FRigElementKey>& rigKeys,
	const TArray<FName>& skipList,
	const TArray<FName>& animationChannelSkipList,
	bool bIncludeAnimationChannels,
	TArray<FRigElementKey>& outKeyedControls,
	float tolerance
)
{
	outKeyedControls.Reset();

	UControlRig* controlRig = rigBinding.ControlRig;
	UMovieSceneControlRigParameterTrack* track = rigBinding.Track;
	if (!IsValid(sequence) || !IsValid(controlRig) || !IsValid(track) || rigKeys.IsEmpty())
	{
		return 0;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return 0;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	TSet<FName> processedControls;
	processedControls.Reserve(rigKeys.Num());

	auto trySetCurrentControl = [controlRig, track, hierarchy, currentFrame, tolerance, &skipList, &animationChannelSkipList, &processedControls, &outKeyedControls](
		const FRigElementKey& controlKey,
		const FRigElementKey* parentControlKey
	) -> bool
	{
		if (controlKey.Type != ERigElementType::Control || controlKey.Name.IsNone() || processedControls.Contains(controlKey.Name))
		{
			return false;
		}

		const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
		if (!controlElement || !hierarchy->IsAnimatable(controlElement))
		{
			return false;
		}

		if (
			isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, skipList) ||
			isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, animationChannelSkipList) ||
			isAnimationControlElementSkipped(hierarchy, controlKey, controlElement, animationChannelSkipList) ||
			(parentControlKey && isAnimationChannelSkipped(parentControlKey->Name, controlKey.Name, animationChannelSkipList))
		)
		{
			processedControls.Add(controlKey.Name);
			return false;
		}

		processedControls.Add(controlKey.Name);
		if (setControlCurrentValueDirectIfModified(controlRig, track, controlElement, currentFrame, tolerance))
		{
			outKeyedControls.Add(controlKey);
			return true;
		}

		return false;
	};

	for (const FRigElementKey& rigKey : rigKeys)
	{
		if (rigKey.Type != ERigElementType::Control)
		{
			continue;
		}

		const FRigElementKey controlKey(rigKey.Name, ERigElementType::Control);
		trySetCurrentControl(controlKey, nullptr);

		if (!bIncludeAnimationChannels)
		{
			continue;
		}

		for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
		{
			if (childKey.Type != ERigElementType::Control)
			{
				continue;
			}

			const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
			if (!childControlElement || !childControlElement->IsAnimationChannel())
			{
				continue;
			}

			trySetCurrentControl(childKey, &controlKey);
		}
	}

	if (!outKeyedControls.IsEmpty())
	{
		track->Modify();
		ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	}

	return outKeyedControls.Num();
}
int32 UReadControlRigKeyLibrary::setThisControlZeroValue(
	ULevelSequence* sequence,
	const FControlRigSequencerBindingProxy& rigBinding,
	FName ctrlName,
	const TArray<FName>& skipList,
	const TArray<FName>& animationChannelSkipList,
	bool bIncludeAnimationChannels,
	TArray<FRigElementKey>& outKeyedControls
)
{
	outKeyedControls.Reset();

	UControlRig* controlRig = rigBinding.ControlRig;
	if (!IsValid(sequence) || !IsValid(controlRig) || ctrlName.IsNone() || skipList.Contains(ctrlName))
	{
		return 0;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return 0;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	const FRigElementKey controlKey(ctrlName, ERigElementType::Control);
	const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
	if (!controlElement || !hierarchy->IsAnimatable(controlElement))
	{
		return 0;
	}

	if (
		isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, skipList) ||
		isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, animationChannelSkipList) ||
		isAnimationControlElementSkipped(hierarchy, controlKey, controlElement, animationChannelSkipList)
	)
	{
		return 0;
	}

	if (setControlZeroValue(sequence, controlRig, controlElement, currentFrame))
	{
		outKeyedControls.Add(controlKey);
	}

	if (bIncludeAnimationChannels)
	{
		for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
		{
			if (
				childKey.Type != ERigElementType::Control ||
				isControlSkippedByNameOrQualifiedName(hierarchy, childKey, skipList) ||
				isAnimationChannelSkipped(controlKey.Name, childKey.Name, animationChannelSkipList)
			)
			{
				continue;
			}

			const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
			if (!childControlElement || !childControlElement->IsAnimationChannel() || !hierarchy->IsAnimatable(childControlElement))
			{
				continue;
			}

			if (setControlZeroValue(sequence, controlRig, childControlElement, currentFrame))
			{
				outKeyedControls.Add(childKey);
			}
		}
	}

	return outKeyedControls.Num();
}

int32 UReadControlRigKeyLibrary::setControlZeroValues(
	ULevelSequence* sequence,
	const FControlRigSequencerBindingProxy& rigBinding,
	const TArray<FRigElementKey>& rigKeys,
	const TArray<FName>& skipList,
	const TArray<FName>& animationChannelSkipList,
	bool bIncludeAnimationChannels,
	TArray<FRigElementKey>& outKeyedControls
)
{
	outKeyedControls.Reset();

	UControlRig* controlRig = rigBinding.ControlRig;
	UMovieSceneControlRigParameterTrack* track = rigBinding.Track;
	if (!IsValid(sequence) || !IsValid(controlRig) || !IsValid(track) || rigKeys.IsEmpty())
	{
		return 0;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return 0;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	TSet<FName> processedControls;
	processedControls.Reserve(rigKeys.Num());

	auto tryZeroControl = [controlRig, track, hierarchy, currentFrame, &skipList, &animationChannelSkipList, &processedControls, &outKeyedControls](
		const FRigElementKey& controlKey,
		const FRigElementKey* parentControlKey
	) -> bool
	{
		if (controlKey.Type != ERigElementType::Control || controlKey.Name.IsNone() || processedControls.Contains(controlKey.Name))
		{
			return false;
		}

		const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
		if (!controlElement || !hierarchy->IsAnimatable(controlElement))
		{
			return false;
		}

		if (
			isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, skipList) ||
			isControlSkippedByNameOrQualifiedName(hierarchy, controlKey, animationChannelSkipList) ||
			isAnimationControlElementSkipped(hierarchy, controlKey, controlElement, animationChannelSkipList) ||
			(parentControlKey && isAnimationChannelSkipped(parentControlKey->Name, controlKey.Name, animationChannelSkipList))
		)
		{
			processedControls.Add(controlKey.Name);
			return false;
		}

		processedControls.Add(controlKey.Name);
		if (setControlZeroValueDirect(controlRig, track, controlElement, currentFrame))
		{
			outKeyedControls.Add(controlKey);
			return true;
		}

		return false;
	};

	for (const FRigElementKey& rigKey : rigKeys)
	{
		if (rigKey.Type != ERigElementType::Control)
		{
			continue;
		}

		const FRigElementKey controlKey(rigKey.Name, ERigElementType::Control);
		tryZeroControl(controlKey, nullptr);

		if (!bIncludeAnimationChannels)
		{
			continue;
		}

		for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
		{
			if (childKey.Type != ERigElementType::Control)
			{
				continue;
			}

			const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
			if (!childControlElement || !childControlElement->IsAnimationChannel())
			{
				continue;
			}

			tryZeroControl(childKey, &controlKey);
		}
	}

	
	if (!outKeyedControls.IsEmpty())
	{
		track->Modify();
		ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	}

	return outKeyedControls.Num();
}

bool UReadControlRigKeyLibrary::duplicatePreviousControlKeyAtCurrentTime(
	const FControlRigSequencerBindingProxy& rigBinding,
	const FRigElementKey& rigKey
)
{
	UControlRig* controlRig = rigBinding.ControlRig;
	UMovieSceneControlRigParameterTrack* track = rigBinding.Track;

	if (!IsValid(controlRig) || !IsValid(track) || rigKey.Name.IsNone())
	{
		return false;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return false;
	}

	const FRigElementKey controlKey(rigKey.Name, ERigElementType::Control);
	const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
	if (!controlElement || !hierarchy->IsAnimatable(controlElement))
	{
		return false;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	UMovieSceneControlRigParameterSection* section =
		findSectionForControl(track, controlKey.Name, currentFrame);
	if (!section)
	{
		return false;
	}

	section->Modify();

	bool duplicatedAnyChannel = duplicatePreviousControlElementChannels(
		controlRig,
		controlElement,
		section,
		currentFrame
	);

	for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
	{
		if (childKey.Type != ERigElementType::Control)
		{
			continue;
		}

		const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
		if (!childControlElement || !childControlElement->IsAnimationChannel() || !hierarchy->IsAnimatable(childControlElement))
		{
			continue;
		}

		UMovieSceneControlRigParameterSection* childSection =
			findSectionForControl(track, childKey.Name, currentFrame);
		if (childSection && childSection != section)
		{
			childSection->Modify();
		}

		duplicatedAnyChannel |= duplicatePreviousControlElementChannels(
			controlRig,
			childControlElement,
			childSection ? childSection : section,
			currentFrame
		);
	}

	if (duplicatedAnyChannel)
	{
		track->Modify();
		ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	}

	return duplicatedAnyChannel;
}

int32 UReadControlRigKeyLibrary::duplicatePreviousControlKeysAtCurrentTime(
	const FControlRigSequencerBindingProxy& rigBinding,
	const TArray<FRigElementKey>& rigKeys,
	TArray<FRigElementKey>& outDuplicatedKeys
)
{
	outDuplicatedKeys.Reset();

	UControlRig* controlRig = rigBinding.ControlRig;
	UMovieSceneControlRigParameterTrack* track = rigBinding.Track;
	if (!IsValid(controlRig) || !IsValid(track) || rigKeys.IsEmpty())
	{
		return 0;
	}

	const URigHierarchy* hierarchy = controlRig->GetHierarchy();
	if (!hierarchy)
	{
		return 0;
	}

	const FFrameNumber currentFrame = getCurrentSequencerFrame();
	TSet<FName> processedControls;
	processedControls.Reserve(rigKeys.Num());

	auto tryDuplicateControl = [controlRig, track, hierarchy, currentFrame, &processedControls, &outDuplicatedKeys](
		const FRigElementKey& controlKey
	) -> bool
	{
		if (controlKey.Type != ERigElementType::Control || controlKey.Name.IsNone() || processedControls.Contains(controlKey.Name))
		{
			return false;
		}

		processedControls.Add(controlKey.Name);

		const FRigControlElement* controlElement = hierarchy->Find<FRigControlElement>(controlKey);
		if (!controlElement || !hierarchy->IsAnimatable(controlElement))
		{
			return false;
		}

		UMovieSceneControlRigParameterSection* section = findSectionForControl(track, controlKey.Name, currentFrame);
		if (!section)
		{
			return false;
		}

		section->Modify();
		if (duplicatePreviousControlElementChannels(controlRig, controlElement, section, currentFrame))
		{
			outDuplicatedKeys.Add(controlKey);
			return true;
		}

		return false;
	};

	for (const FRigElementKey& rigKey : rigKeys)
	{
		if (rigKey.Type != ERigElementType::Control)
		{
			continue;
		}

		const FRigElementKey controlKey(rigKey.Name, ERigElementType::Control);
		tryDuplicateControl(controlKey);

		for (const FRigElementKey& childKey : hierarchy->GetChildren(controlKey, true))
		{
			if (childKey.Type != ERigElementType::Control)
			{
				continue;
			}

			const FRigControlElement* childControlElement = hierarchy->Find<FRigControlElement>(childKey);
			if (!childControlElement || !childControlElement->IsAnimationChannel())
			{
				continue;
			}

			tryDuplicateControl(childKey);
		}
	}

	if (!outDuplicatedKeys.IsEmpty())
	{
		track->Modify();
		ULevelSequenceEditorBlueprintLibrary::RefreshCurrentLevelSequence();
	}

	return outDuplicatedKeys.Num();
}
