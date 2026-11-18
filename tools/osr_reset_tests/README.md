# OSR native history reset regression

Windows integration test of the production `Afw.cpp` adapter and `Osr.cpp` shaders
using Microsoft's D3D12 WARP software adapter. No Cyberpunk process, OpenXR session,
PureDark DLL, game installation or simulator is needed. Requires CMake 3.24+, a
C++20 Windows toolchain, Windows SDK and OpenXR SDK headers.

```powershell
cmake -S tools/osr_reset_tests -B build/osr-reset-tests -DOSR_OPENXR_INCLUDE_DIR="C:/path/to/OpenXR-SDK/include"
cmake --build build/osr-reset-tests --config Release --parallel 2
ctest --test-dir build/osr-reset-tests -C Release --output-on-failure -V
```

The synthetic scene changes a uniform plane from red to blue while alternating
eyes. Its unchanged geometry allows old red history to pass depth validation at
disocclusions. A negative control proves the fixture exposes old colour. The test
then supplies `Inputs::camera.reset` through the real adapter and checks that the
OSR neither enables old history nor emits red pixels. Every output pixel must be
the current blue scene; an empty frame cannot pass.

Additional checks cover the next-frame recovery, consecutive reset-marked frames,
history explicitly disabled, an unchanged uniform scene and disabled AFW/OSR launch.
They also exercise gaps, duplicate/backward engine frame IDs, same-eye repetition,
natural Uint32 wrap, the production capture guard, backend/launch round trips,
recorded-but-unsubmitted command lists, cross-thread invalidation (including a
request after recording), resize and replacement devices. FOV/IPD changes alone
preserve valid world-space history. A simulated interruption uses the same guard
as the production menu/failure path, but does not execute the game's UI or XR lifecycle.
Checks remain enabled in Release. The fixture waits on its own GPU fence before
reusing test resources or reading output; these waits do not change runtime code.
The D3D12 debug layer is used when installed, with errors/corruption treated as
failures. The executable explicitly reports when validation is unavailable.

Before the fix, the test fails at `native camera reset must reach production OSR`.
This test demonstrates native-reset propagation and GPU reconstruction output in a
controlled scene. It does not measure performance or validate headset comfort,
gameplay event coverage or the PureDark algorithm. A missed native reset across a
gap is covered by the conservative continuity rule; identifying the real game's
cut/loading signals still requires the deferred in-game matrix.

Scope and remaining gates: [OSR history reset](../../docs/osr-history-reset.md).
