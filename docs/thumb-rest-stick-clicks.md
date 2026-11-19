# Thumb-rest stick-click fallback

Base: fork main, 0.2.45. Branch: `feature/thumb-rest-stick-clicks-0.2.45`.

`CONTROLS > META QUEST > Thumb rests as L3 / R3` adds the live, persisted INI setting
`xr_thumbrest_stick_click=0` (off by default). Enabled behavior, per physical hand:

This is a provisional mapping until a control rebinding and fine-tuning system is
implemented. The META QUEST page states this explicitly; it is not a general rebind UI.

```text
L3 = left stick click  OR left thumb-rest touch
R3 = right stick click OR right thumb-rest touch
```

Touch is a held input, released when both sources are released; physical clicks remain
available. There is no pulse, latch or extra repeat. The merge happens before existing
scanner, cyberware, D-pad, deferred L3, menu and handedness routing. Left-click validity
accepts either active action so an inactive stick-click action cannot disable the rest
fallback. Failed/inactive actions never contribute stale touch values. Thumb-rest reads
also require `xrSyncActions == XR_SUCCESS`, preventing cached touch after failed or
unfocused synchronization. Physical-click routing retains its existing behavior.

The per-hand Boolean action binds `/input/thumbrest/touch` only on the existing Oculus
Touch profile, inside its single binding suggestion. Other profiles are unchanged; an
unbound/inactive action does nothing. For a Quest selection and matching OpenXR system,
the action is created at
startup regardless of the toggle, allowing live changes without reattaching action sets. Its handle is cleared at
shutdown. Pose-only mode neither creates nor binds gameplay actions.

The launcher's persisted selection must be Quest 2 (ID 0), Quest 3S (ID 1) or Quest 3
(ID 2). OpenXR must also identify a supported Quest system (Quest 2/3S/3, with optional
Meta/Oculus prefix, ignoring case and spacing). Unknown names fail closed. Both hands
must report the actual selected `/interaction_profiles/oculus/touch_controller`
profile after a successful sync. Missing, unbound, incompatible or failed profile
queries hide the entire META QUEST page and block touch acquisition, even when the
INI preference is enabled. Physical clicks remain available. When support is lost
while the page is selected, Controls returns to GENERAL. The saved preference is
retained for returning to a supported headset.

`QueryThumbRestSupport` runs after each sync; `OpenXRManager::IsThumbRestSupported`
publishes its result to both the Controls page and input activation. This avoids
trusting the launcher alone (for example, Quest selected while OpenXR reports Steam
Frame), or treating a successful binding suggestion as the active controller profile.

OpenXR identity is runtime-reported, not hardware attestation. A streaming runtime
can emulate both the Quest system name and Touch profile; the codebase already records
VDXR reporting `Oculus Quest2` for Pico 4. Such a fully masked device plus an incorrect
Quest launcher selection cannot be distinguished through these core OpenXR reports.
See the [OpenXR current-profile rules](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/input.adoc#input-current-interaction-profile).

Both rests held also participate in the existing half-second L3+R3 menu gesture. This
is intentional under the OR mapping and is stated in the tooltip. Existing scanner and
other chord ownership rules still take precedence over deferred gameplay L3.

The component is defined by the [OpenXR Oculus Touch interaction profile](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#semantic-paths-oculus-touch-controller-profile).
Runtime/controller exposure must still be verified in the headset; trackpad or stick
touch on another profile is not substituted for thumb-rest touch.

## Validation

2026-11-19: user confirmed the original feature working in the headset. Follow-up
adds launcher plus OpenXR checks, the conditional META QUEST page, provisional-mapping
notice and sync-result guard. Regression coverage includes all 19 current launcher
presets, unknown IDs/names, accepted Quest names, launcher/OpenXR mismatch, either
hand using an incompatible or unbound profile, profile-query failure and recovery,
physical clicks outside Quest, cached touch after sync failure, and unfocused sync.
The gated build still needs an in-game check of the menu and runtime identity.
Both Release x64 input suites passed (`thumbrest_input`, `scanner_input`), as did
`git diff --check`. The existing `bthumb` Release x64 plugin project recompiled the
changed production files and dependents of the OpenXR manager header, then linked successfully using its
game-2.5 SDK override; dependency rebuilds were skipped. No game files were deployed.

Release x64 tests compile the production action-read and OR-merge fragments from
OpenXRFrameLoop, with mocked OpenXR state calls. Passed: default/off, physical input,
per-hand separation, inactive/failed input, missing action, active fallback validity,
overlapping sources, release edges, claimed L3 and the existing half-second menu chord.
Existing scanner-input regression passed. The full Release x64 plugin linked from
commit `0c3a5b92` (implementation `2538a966`). DLL SHA256:
`1106d0bb4c61569114d86b8a64a0b61f03ae66a3142ba2654e70a0516e708044`.
Build used game-2.5 SDK override `5246e659d783e5834cc08a90e56c8932c9ed3449` and a local
guard for the absent optional `tools/rtti_dump`; the guard and SDK gitlink changes are
not part of this feature. The test artifact is base 0.2.45 plus this input feature;
it does not include the separate VRS, OSR reset or F10 patches.

The latest available game log identified Oculus Touch on both hands under VirtualDesktopXR.
The user confirmed original thumb-rest behavior in the headset on 2026-11-19. The new
OpenXR identity gate, conditional page and UI persistence still need in-game validation.
No deployed game DLL/configuration was changed by this patch.

Manual check: enable the option, test each rest/click separately and overlapping, remove
both thumbs, test both-rest menu hold, scanner/D-pad ownership, disable while touching,
then restart and verify the saved setting. Check input-focus loss and an unsupported
controller profile. Confirm physical clicks still behave as before with the option off.
