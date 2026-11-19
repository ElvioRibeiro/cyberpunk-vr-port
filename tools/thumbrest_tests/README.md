# Thumb-rest input regression

```powershell
cmake -S tools/thumbrest_tests -B build/thumbrest-tests -A x64 -DTHUMBREST_OPENXR_INCLUDE_DIR=<OpenXR-SDK>/include
cmake --build build/thumbrest-tests --config Release
ctest --test-dir build/thumbrest-tests -C Release --output-on-failure
```

The harness compiles the actual OpenXRFrameLoop action-read/merge fragments, with
mocked OpenXR calls, the real default live setting and existing deferred-L3/menu chord
policies. It needs no game, OpenXR runtime or headset.
Headset isolation covers Quest presets, all other launcher presets and unknown IDs;
sync cases reject cached touch after failure or focus loss.
OpenXR checks cover supported/unknown system names, a wrong launcher selection,
either hand's incompatible/unbound profile, failed profile queries and recovery.
The production eligibility helper is used by the fixture. It does not validate runtime
binding support, UI rendering or INI round-trip in a running game.
