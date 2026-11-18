# Stereo foveated VRS

Base: fork main, 0.2.45. Planning only; rendering changes are not implemented.

- Profile the native VRS baseline and identify eligible opaque passes and both eye rectangles.
- Extend the existing backend with a target-aligned stereo map; start with 1x1 centers and 2x2 periphery.
- Preserve native state outside approved draws, resource lifetime and a pass-through Off mode.
- Validate both eyes, state restoration and net GPU benefit before expanding coverage.

Eye tracking, 4x4, dynamic quality and OSR pacing changes are outside the initial scope.
