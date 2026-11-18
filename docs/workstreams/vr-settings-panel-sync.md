# F10 VR settings canvas synchronization

Base: fork main, 0.2.45. Implementation: `src/Overlay/VrPanel.cpp`.

## Change

The ImGui canvas is rendered on the game queue and copied by the runtime queue. The old consumer assumed both queues were the same and sampled the newest publication without checking its producer fence.

The panel now leases the newest completed canvas from the existing three-slot pool. Reacquiring a producer slot withdraws its previous publication before recording begins. While no update is ready, the panel retains its last released swapchain image. Closing withdraws all publications; leased resources stay alive until their consumers finish. Removed-device fence values are rejected.

No CPU/GPU wait, additional texture allocation or pool expansion was added. Updates can appear later when their producer has not completed; this change makes no FPS claim.

## Validation

- Release x64 regression builds the production panel and ColorBlit shader, with real D3D12 WARP producer/runtime queues and mocked OpenXR swapchains.
- Passed: unfinished first publication rejected; older finished publication selected; last valid image retained; completed update delivered; producer-owned slots excluded; leases preserved; close/reopen cleared stale publications. Released-image RGB/alpha is read back from the GPU.
- Negative control with the original 0.2.45 panel fails at `unfinished first canvas submitted`; corrected panel passes (1/1).
- D3D12 debug layer was unavailable. Full plugin build and headset validation remain pending.

## In-game check

Compare the same scene/settings before and after: repeatedly open F10, move the pointer/change pages, then close and reopen it, with frame generation off and on. Check both eyes, stale content and frame pacing. A delayed update is acceptable while the producer is busy; alternating visibility is not.

This patch covers the ImGui F10 panel. The world Ink surface used by shop/held-Y weapon menus needs separate investigation on `fix/vr-menu-surface-flicker-0.2.45`. OSR reset remains a separate PR.
