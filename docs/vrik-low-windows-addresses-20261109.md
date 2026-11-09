# Stationary Quest 2 controller-driven hands: low Windows allocations (2026-11-09)

## Evidence

The supplied `temp/` diagnostics reproduce stationary avatar hands with working Touch buttons and sticks.
The first run's `vrik_diag.txt` contains changing controller coordinates, but no resolved skeleton:
`headIdx=-1`, `boneCount=0`, `lastBoneBuf=0`, `matchCalls=0`, including the final capture with `bind=4`.
The second, verbose run reports both hands active with valid located poses in multiple samples, while
`poseApply` and `freshSolve` remain zero. Later samples also contain inactive controllers; the fix below
does not address runtime focus or tracking loss.

`animation_system_lookup.txt` reports `playerEntity=0x5978fb7f0`, `runtimeScene=0x1a0057e98`, no registered
animation-system game system, and `scannedAnimationSystem=0`. These are approximately 6–24 GiB addresses,
below the Windows-only 1 TiB floor in `ClassifyQword`. The known scene chain rejects a low parent/system,
and arming separately rejects a low metaRig. The supplied dumps do not print those three pointers, so
the exact rejected hop still requires a corrected-build run to confirm.

## Changes

- `src/Natives/AnimLookup.cpp`: validate readable memory for the known runtime scene and parent chain;
  validate the candidate's readable object header and retain the `worldAnimationSystem` RTTI check.
  Cached-system validation uses the same guard. No minimum heap address is assumed in this typed path.
- `src/Natives/VrikArming.cpp`: validate the known metaRig's complete header with `VRIK_IsReadable`
  instead of applying the heap-address classifier. Bone counts and binding checks remain in place.
- Untyped fallback scans and `ClassifyQword` remain unchanged. This avoids admitting additional arbitrary
  pointers into a scan that calls virtual RTTI methods.
- `tools/vrik_tests/low_address_lookup_test.py`: extract the production lookup and readable-memory
  functions, compile them with MSVC, and exercise real Windows allocations and protection flags.
  RTTI is a mocked boundary; the test does not launch the game or exercise the complete arming routine.

## Validation and remaining work

Run on Windows with MSVC x64 build tools:

```powershell
python tools/vrik_tests/low_address_lookup_test.py
```

Cases cover valid chains below 4 GiB, at 8 GiB, and at 2 TiB; wrong RTTI; null/invalid pointers;
an inaccessible scene/candidate; and a guarded parent page. The smoke test passed locally.

The complete plugin has not been built or deployed in this checkout: Cargo, required by the root CMake
configuration to build the bundled redscript compiler, is not installed/on PATH (MSVC is available).
A game run must confirm nonzero
bone count, resolved head and arm indices, increasing `matchCalls`/`freshSolve`, and hands following
both controllers. Test across save reload and equipment changes as these rebind the skeleton.
