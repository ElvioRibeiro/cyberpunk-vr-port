# Stereo foveated VRS

Base: fork main, 0.2.45. Experimental implementation on `feature/foveated-vrs-stereo-0.2.45`; disabled by default.

## Implementation

- Extends FixedVrs and its existing draw/state/resource hooks. No second injector or stereo reconstruction.
- Accepts MAIN RenderElements (`0x33DDA4`, game 2.5), the existing opaque pipeline classification, 3–4 compatible MRTs with depth, and a single integral viewport covering exactly one half of an even-width side-by-side target. Physical eye identity comes from that viewport, not MAIN/VRCAM.
- Uses one target-aligned map with two geometric centers. Center width/height/offset remain relative to each eye. Tiles intersecting a center or straddling the eye boundary stay 1x1; other tiles use 2x2. Stereo requests for 4x4 are effectively limited to 2x2; legacy behavior remains available outside engine stereo.
- Shares immutable maps across equivalent layouts. Existing bounded cache and command-resource leases retain maps for recorded/submitted GPU work. No per-draw upload or GPU wait is added.
- Missing/mismatched targets, arrays, MSAA, nonzero RTV mips, ambiguous viewports, mono frames, non-MAIN views, unknown bundles/render passes and unclassified indirect signatures preserve native VRS. Copied RTV metadata is invalidated until descriptor re-creation.
- Restores the native image, base rate and both combiners outside eligible draws. Off remains pass-through. UI/report distinguishes the requested/effective mode, map readiness and cumulative applied draws for each physical eye.

The node RVA matches `NodeNames.inc`/`NodeDispatch.cpp` in this base. The test's old `0x23A938` was the RenderElements address from 2.31; it was updated to exercise the current backend gate, not to expand the allowlist.

## Validation

Standalone Release x64 CPU and hardware-GPU tests passed (2/2). GPU regression uses a 520x192 target: each 260px eye places the split inside a 16px tile. It reads all three MRTs and checks both centers/seam, peripheral changes in both eyes, the complete native-state tuple, fallback, Off and reuse across 32 frames. CPU cases cover offsets, tile sizes, fractional/odd/unknown layouts, RTV mip/array/MSAA rejection and descriptor invalidation/re-creation.

Synthetic stereo PS invocations: 99,840 baseline versus 65,664 with VRS. This is shader-work evidence from the fixture, not frame time or a game FPS prediction. The local D3D12 debug layer was unavailable. The GPU test returns CTest skip code 77 on hardware without Tier 2 instead of reporting a pass.

Full plugin build, game-pass coverage/profile, D3D12 debug validation and headset quality/benefit must be recorded separately. Keep this feature experimental until those checks pass. The existing pipeline classifier does not prove that every real shader's alpha testing, motion vectors, derivatives or side effects are suitable.

## In-game validation

1. Record DLL hash, GPU/driver, runtime, resolution and upscaler. Use native engine stereo, FG/RT/PT/dynamic resolution off for the first comparison; keep the scene/settings fixed.
2. Compare Off, an all-1x1 map (100% center width/height), and 2x2 with a 70% x 65% center. Allow settings debounce and map preparation. Confirm applied-draw counters increase for both eyes; map readiness alone does not prove coverage.
3. Check peripheral motion, vegetation, material edges and center readability in both eyes. Visit menu/loading/save transitions and restore Off. Inspect GPU frame time, tail latency and VRAM over repeated equivalent runs.
4. Profile actual eligible passes and their native VRS policy before claiming benefit or expanding coverage. A zero counter means this layout/pipeline remains native; do not remove guards merely to increase coverage.

Eye tracking, projection-centered regions, 4x4 stereo, dynamic quality, pass expansion, OSR/reset and F10 changes are separate work.

The target-space contract follows [the DirectX VRS specification](https://microsoft.github.io/DirectX-Specs/d3d/VariableRateShading.html): image origin is the target origin, so concatenating independently rounded eye maps is incorrect.
