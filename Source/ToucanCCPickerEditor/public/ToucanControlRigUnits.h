#pragma once

#include "CoreMinimal.h"
#include "Units/RigUnit.h"
#include "Rigs/RigHierarchyDefines.h"
#include "ToucanControlRigUnits.generated.h"

UENUM(BlueprintType)
enum class EToucanControlRigKeyValueType : uint8
{
	FloatChannel UMETA(DisplayName = "Float Channel")
};

USTRUCT(BlueprintType)
struct TOUCANCCPICKEREDITOR_API FToucanWeightedCurve
{
	GENERATED_BODY()

	FToucanWeightedCurve()
		: CurveName(NAME_None)
		, Weight(1.f)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input, CustomWidget = "CurveName"))
	FName CurveName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float Weight;
};

USTRUCT(BlueprintType)
struct TOUCANCCPICKEREDITOR_API FToucanCurveValueMapping
{
	GENERATED_BODY()

	FToucanCurveValueMapping()
		: Name(NAME_None)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input, ExpandByDefault))
	TArray<FToucanWeightedCurve> Curves;
};

USTRUCT(BlueprintType)
struct TOUCANCCPICKEREDITOR_API FToucanTongueJawValue
{
	GENERATED_BODY()

	FToucanTongueJawValue()
		: CurveName(NAME_None)
		, UpperJawAdditive(FTransform::Identity)
		, LowerJawAdditive(FTransform::Identity)
		, TongueOut(0.f)
		, TongueVertical(0.f)
		, TongueHorizontal(0.f)
		, TongueTipVertical(0.f)
		, TongueTipHorizontal(0.f)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input, CustomWidget = "CurveName"))
	FName CurveName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	FTransform UpperJawAdditive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	FTransform LowerJawAdditive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float TongueOut;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float TongueVertical;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float TongueHorizontal;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float TongueTipVertical;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	float TongueTipHorizontal;
};


USTRUCT(BlueprintType)
struct TOUCANCCPICKEREDITOR_API FToucanCurveValueTongueAndJawMapping
{
	GENERATED_BODY()

	FToucanCurveValueTongueAndJawMapping()
		: Name(NAME_None)
	{}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input))
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toucan|Controls", meta = (Input, ExpandByDefault))
	TArray<FToucanTongueJawValue> TongueJawValues;
};

USTRUCT(meta = (DisplayName = "Get Highest Control Value", Category = "Toucan|Controls", Keywords = "Toucan,ControlRig,Float,Channel,Highest,Max", NodeColor = "0.0 0.364706 1.0"))
struct TOUCANCCPICKEREDITOR_API FRigUnit_ToucanGetHighestControlValue : public FRigUnit
{
	GENERATED_BODY()

	FRigUnit_ToucanGetHighestControlValue()
		: KeyValueType(EToucanControlRigKeyValueType::FloatChannel)
		, HighestValue(0.f)
		, bFound(false)
	{}

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FRigElementKey> Keys;

	UPROPERTY(meta = (Input))
	EToucanControlRigKeyValueType KeyValueType;

	UPROPERTY(meta = (Output))
	FName HighestControlName;

	UPROPERTY(meta = (Output))
	FRigElementKey HighestKey;

	UPROPERTY(meta = (Output))
	float HighestValue;

	UPROPERTY(meta = (Output))
	bool bFound;

	UPROPERTY()
	TArray<FCachedRigElement> CachedKeys;
};

USTRUCT(meta = (DisplayName = "Set Mapped Curve Values", Category = "Toucan|Controls", Keywords = "Toucan,ControlRig,Curve,Mapping,Weight", NodeColor = "0.0 0.364706 1.0"))
struct TOUCANCCPICKEREDITOR_API FRigUnit_ToucanSetMappedCurveValues : public FRigUnitMutable
{
	GENERATED_BODY()

	FRigUnit_ToucanSetMappedCurveValues()
		: Name(NAME_None)
		, Value(0.f)
		, AppliedCurveCount(0)
		, bFoundMapping(false)
	{}

	RIGVM_METHOD()
	virtual void Execute() override;

	UPROPERTY(meta = (Input, ExpandByDefault))
	TArray<FToucanCurveValueMapping> Mappings;

	UPROPERTY(meta = (Input))
	FName Name;

	UPROPERTY(meta = (Input))
	float Value;

	UPROPERTY(meta = (Output))
	int32 AppliedCurveCount;

	UPROPERTY(meta = (Output))
	bool bFoundMapping;

	UPROPERTY()
	TArray<FCachedRigElement> CachedCurves;
};
