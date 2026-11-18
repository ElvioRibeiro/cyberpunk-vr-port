# Thumb-rest stick-click fallback

Base: fork main, 0.2.45. Branch: `feature/thumb-rest-stick-clicks-0.2.45`.

`CONTROLS > GENERAL > Thumb rests as L3 / R3` adds the live, persisted INI setting
`xr_thumbrest_stick_click=0` (off by default). Enabled behavior, per physical hand:

```text
L3 = left stick click  OR left thumb-rest touch
R3 = right stick click OR right thumb-rest touch
```

Touch is a held input, released when both sources are released; physical clicks remain
available. There is no pulse, latch or extra repeat. The merge happens before existing
scanner, cyberware, D-pad, deferred L3, menu and handedness routing. Left-click validity
accepts either active action so an inactive stick-click action cannot disable the rest
fallback. Failed/inactive actions never contribute stale touch values. No synthetic
state survives a failed input sync: the existing controller snapshot reset still applies.

The per-hand Boolean action binds `/input/thumbrest/touch` only on the existing Oculus
Touch profile, inside its single binding suggestion. Other profiles are unchanged; an
unbound/inactive action does nothing. The action is created at startup regardless of the
toggle, allowing live changes without reattaching action sets. Its handle is cleared at
shutdown. Pose-only mode neither creates nor binds gameplay actions.

Both rests held also participate in the existing half-second L3+R3 menu gesture. This
is intentional under the OR mapping and is stated in the tooltip. Existing scanner and
other chord ownership rules still take precedence over deferred gameplay L3.

The component is defined by the [OpenXR Oculus Touch interaction profile](https://registry.khronos.org/OpenXR/specs/1.0-khr/html/xrspec.html#semantic-paths-oculus-touch-controller-profile).
Runtime/controller exposure must still be verified in the headset; trackpad or stick
touch on another profile is not substituted for thumb-rest touch.

## Validation

Release x64 tests compile the production action-read and OR-merge fragments from
OpenXRFrameLoop, with mocked OpenXR state calls. Passed: default/off, physical input,
per-hand separation, inactive/failed input, missing action, active fallback validity,
overlapping sources, release edges, claimed L3 and the existing half-second menu chord.
Existing scanner-input regression passed. Full plugin build and hardware testing are
recorded separately; no deployed game DLL/configuration was changed by this patch.

Manual check: enable the option, test each rest/click separately and overlapping, remove
both thumbs, test both-rest menu hold, scanner/D-pad ownership, disable while touching,
then restart and verify the saved setting. Check input-focus loss and an unsupported
controller profile. Confirm physical clicks still behave as before with the option off.
