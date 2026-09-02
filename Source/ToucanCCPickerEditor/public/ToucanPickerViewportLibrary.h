#pragma once

#include "CoreMinimal.h"
#include "ControlRig.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ToucanPickerViewportLibrary.generated.h"

UENUM(BlueprintType)
enum class EToucanControlAxis : uint8
{
    PositiveX UMETA(DisplayName = "+X"),
    NegativeX UMETA(DisplayName = "-X"),
    PositiveY UMETA(DisplayName = "+Y"),
    NegativeY UMETA(DisplayName = "-Y"),
    PositiveZ UMETA(DisplayName = "+Z"),
    NegativeZ UMETA(DisplayName = "-Z")
};

UCLASS()
class TOUCANCCPICKEREDITOR_API UToucanPickerViewportLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Toucan|Viewport")
    static void focusViewportOnControl(
        const FTransform& controlTransform,
        EToucanControlAxis alignAxis,
        float cameraDistance = 100.0f
    );

    UFUNCTION(BlueprintCallable, Category="Toucan|Control Rig")
    static int32 setControlRigControlSelection(
        UControlRig* controlRig,
        const TArray<FName>& controlNames,
        bool clearPrevious,
        bool toggleSelection,
        FString& errorMessage
    );
};
