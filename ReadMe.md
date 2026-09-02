## Requirements
The "Toucan" Picker and Rig build upon the Picker and Rig made by CC. They control Character Creator characters from Reallusion. Therefore you will need the plugins from Reallusion (you can use the auto plugin for example).

The Picker also uses functions from the SequencerAbstraction repo: https://github.com/J-Andersen-UvA/sequencerAbstraction


## Functionalities
Besides the present functionalities from the CC Rig and Picker, the Toucan version Picker adds:
- A more compact picker
- The ability to shift/ctrl click the picker (so you can multi-select and deselect)
- Zeroing keyframe shortcut
- ReKey shortcut
- Finger and body control visualize option that removes old keys where necessary
- LookAt offset selector
- Ability to see what values are altered at the current playhead time for the rig
- Buttons to quickly move camera to set angles of the hand
- Additional picker assets for faster access to specialized control sets:
  - `Toucan_CC_Mouthing_Picker` for blending to Oral Components and shaping mouth poses
  - `Toucan_CC_Tongue_Picker` for moving tongue controls directly
  - `Toucan_CC_Selection_Tool` for easier selection of Forward Control Rig controls

The version of the Toucan Rig adds:
- LookAt offset for eyebones
- Hands separated from the body for show bool
- Default to IK
- Mapped curve helpers for blending oral components into jaw, mouth, and tongue control values

## Blend to base pose
Blending the animation back to the base pose deserves extra attention in this readme. Here is the flow inside the control rig that manages the return to the base pose.
```mermaid
---
title: Rig flow for blending to base pose 2.0
---
flowchart TD


    subgraph A[Construct Event]
        A0[Cache resting bone transforms<br/>FingersResting]
    end

	subgraph B[Backwards Solve]
		B0[Cache incoming animated bone transforms<br/>FingersAnimated]
	end
	
	subgraph C[Forward Solve]
		subgraph CA[Blend Animation Toward Base]
			CA0[Read user base-pose weight]
			CA1[Blend FingersAnimated → FingersResting]
			CA2[Compute correction:<br/>blended pose relative to FingersAnimated]
			CA3[Apply correction as bone offset]
			CA0 --> CA1 --> CA2 --> CA3
		end

		subgraph CB[Apply User Control]
			CB0[Get animated transform relative to resting transform]
			CB1[Get control transform relative to animated-vs-resting offset]
			CB2[Apply result as bone offset]
			CB0 --> CB1 --> CB2
		end
		
		CA <-->|Independent corrections| CB
	end
	
	B --> C
	A --> C	
```