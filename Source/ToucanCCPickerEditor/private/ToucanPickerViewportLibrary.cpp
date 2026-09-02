#include "ToucanPickerViewportLibrary.h"

#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"

namespace
{
    FVector getAlignedAxis(const FTransform& controlTransform, EToucanControlAxis alignAxis)
    {
        switch (alignAxis)
        {
            case EToucanControlAxis::PositiveX:
                return controlTransform.GetUnitAxis(EAxis::X);

            case EToucanControlAxis::NegativeX:
                return -controlTransform.GetUnitAxis(EAxis::X);

            case EToucanControlAxis::PositiveY:
                return controlTransform.GetUnitAxis(EAxis::Y);

            case EToucanControlAxis::NegativeY:
                return -controlTransform.GetUnitAxis(EAxis::Y);

            case EToucanControlAxis::PositiveZ:
                return controlTransform.GetUnitAxis(EAxis::Z);

            case EToucanControlAxis::NegativeZ:
                return -controlTransform.GetUnitAxis(EAxis::Z);

            default:
                return controlTransform.GetUnitAxis(EAxis::X);
        }
    }
}

void UToucanPickerViewportLibrary::focusViewportOnControl(
    const FTransform& controlTransform,
    EToucanControlAxis alignAxis,
    float cameraDistance
)
{
#if WITH_EDITOR
    if (!GEditor)
    {
        return;
    }

    const FVector controlLocation = controlTransform.GetLocation();
    const FVector cameraForward = getAlignedAxis(controlTransform, alignAxis).GetSafeNormal();

    if (cameraForward.IsNearlyZero())
    {
        return;
    }

    const FVector worldUp = FVector::UpVector;
    FVector right = FVector::CrossProduct(worldUp, cameraForward).GetSafeNormal();

    if (right.IsNearlyZero())
    {
        right = FVector::CrossProduct(FVector::ForwardVector, cameraForward).GetSafeNormal();
    }

    const FVector cameraUp = FVector::CrossProduct(cameraForward, right).GetSafeNormal();
    const FVector cameraLocation = controlLocation - (cameraForward * cameraDistance);
    const FRotator cameraRotation = FRotationMatrix::MakeFromXZ(cameraForward, cameraUp).Rotator();

    for (FLevelEditorViewportClient* viewportClient : GEditor->GetLevelViewportClients())
    {
        if (!viewportClient || !viewportClient->IsPerspective())
        {
            continue;
        }

        viewportClient->SetViewLocation(cameraLocation);
        viewportClient->SetViewRotation(cameraRotation);
        viewportClient->Invalidate();
    }
#endif
}

int32 UToucanPickerViewportLibrary::setControlRigControlSelection(
    UControlRig* controlRig,
    const TArray<FName>& controlNames,
    bool clearPrevious,
    bool toggleSelection,
    FString& errorMessage
)
{
    errorMessage.Empty();

    if (!controlRig)
    {
        errorMessage = TEXT("Control Rig is null.");
        return 0;
    }

    URigHierarchy* hierarchy = controlRig->GetHierarchy();
    if (!hierarchy)
    {
        errorMessage = TEXT("Control Rig has no hierarchy.");
        return 0;
    }

    URigHierarchyController* controller = hierarchy->GetController(true);
    if (!controller)
    {
        errorMessage = TEXT("Could not get Control Rig hierarchy controller.");
        return 0;
    }

    TArray<FName> currentNames = controlRig->CurrentControlSelection();
    TSet<FName> desiredNames;

    if (!clearPrevious)
    {
        desiredNames.Reserve(currentNames.Num() + controlNames.Num());
        for (const FName& currentName : currentNames)
        {
            if (!currentName.IsNone())
            {
                desiredNames.Add(currentName);
            }
        }
    }
    else
    {
        desiredNames.Reserve(controlNames.Num());
    }

    int32 invalidNameCount = 0;
    for (const FName& controlName : controlNames)
    {
        if (controlName.IsNone() ||
            !hierarchy->Contains(FRigElementKey(controlName, ERigElementType::Control)))
        {
            ++invalidNameCount;
            continue;
        }

        if (toggleSelection && desiredNames.Contains(controlName))
        {
            desiredNames.Remove(controlName);
        }
        else
        {
            desiredNames.Add(controlName);
        }
    }

    TArray<FRigElementKey> desiredKeys;
    desiredKeys.Reserve(desiredNames.Num());
    for (const FName& desiredName : desiredNames)
    {
        desiredKeys.Emplace(desiredName, ERigElementType::Control);
    }

    desiredKeys.Sort([](const FRigElementKey& left, const FRigElementKey& right)
    {
        return left.Name.LexicalLess(right.Name);
    });

    TArray<FRigElementKey> currentKeys;
    currentKeys.Reserve(currentNames.Num());
    for (const FName& currentName : currentNames)
    {
        if (!currentName.IsNone())
        {
            currentKeys.Emplace(currentName, ERigElementType::Control);
        }
    }

    currentKeys.Sort([](const FRigElementKey& left, const FRigElementKey& right)
    {
        return left.Name.LexicalLess(right.Name);
    });

    if (currentKeys != desiredKeys)
    {
        controller->SetSelection(desiredKeys, false);
    }

    if (invalidNameCount > 0)
    {
        errorMessage = FString::Printf(
            TEXT("Skipped %d invalid control name(s)."),
            invalidNameCount);
    }

    return desiredKeys.Num();
}
