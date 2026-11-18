# VR panel synchronization regression

Builds the real VrPanel and ColorBlit on D3D12 WARP. OpenXR swapchain calls and panel tracking/visibility are mocked; no game or headset is required.

```powershell
cmake -S tools/vr_panel_tests -B build/vr-panel-tests -A x64 -DPANEL_OPENXR_INCLUDE_DIR=<OpenXR-SDK>/include
cmake --build build/vr-panel-tests --config Release
ctest --test-dir build/vr-panel-tests -C Release --output-on-failure
```

The producer queue is deliberately gated while the runtime queue remains available. Tests read back the released image to check completed-canvas selection, preservation and subsequent update, plus pool leases, unpublished slots and close/reopen. Test-only GPU waits observe results; production code adds no wait.

For a negative control, point `VR_PANEL_SOURCE` at the original 0.2.45 VrPanel.cpp in a separate build directory. It must fail with `unfinished first canvas submitted`.

This is not validation of a real OpenXR runtime, headset, full plugin build or shop/weapon-wheel Ink menus. See [implementation and manual checks](../../docs/workstreams/vr-settings-panel-sync.md).
