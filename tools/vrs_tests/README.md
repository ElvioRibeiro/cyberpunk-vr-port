# Fixed VRS regression

```powershell
cmake -S tools/vrs_tests -B build/vrs-tests -A x64 -DVRS_OPENXR_INCLUDE_DIR=<OpenXR-SDK>/include
cmake --build build/vrs-tests --config Release
ctest --test-dir build/vrs-tests -C Release --output-on-failure
```

`vrs_settings` checks the pure policy, global stereo tile alignment, both protected centers, seam, invalid viewports/RTV layouts and descriptor metadata invalidation.

`vrs_gpu` compiles the production backend and CommandResources on the selected high-performance adapter. Engine mode/frame flags are fixture-controlled; no game or OpenXR runtime is loaded. It verifies legacy and side-by-side draws, reads three MRTs, counts pixel shader invocations, checks native-state restoration/fallback and exercises map reuse across frames. The stereo fixture deliberately uses an eye width that is not a multiple of a VRS tile.

Tier 2 absence returns 77, reported as skipped by CTest. When Graphics Tools is installed the fixture also checks D3D12 debug messages; otherwise it explicitly reports that debug validation was unavailable.

These tests do not validate the game's actual pass coverage, headset quality, GPU frame time or CPU overhead. See [scope and game checks](../../docs/workstreams/vrs-stereo.md).
