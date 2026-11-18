# OSR history reset

Native camera resets now invalidate both OSR histories before reconstruction.
Capture interruptions, session/backend changes and unproven frame continuity
also invalidate history. Reuse requires consecutive engine frame IDs and
alternating eyes; unsigned frame-counter wrap is accepted.

Only the capture thread changes history state. Other callers request an atomic
invalidation. An aborted capture cannot leave reusable history behind.
Resets preserve allocations and add no captures or GPU waits. Ordinary movement
and FOV/IPD changes do not independently trigger reset.

## Validation

- Production adapter/shaders exercised by the [WARP regression](../tools/osr_reset_tests/README.md): passed.
- Full x64 Release linked locally using the documented CP2.5 SDK override and
  a guard for the absent optional RTTI tool; these build accommodations are outside this change.
- In-game: about 30 minutes of gameplay and two save reloads with normal image recovery.
- Dedicated menu-cycle validation and the complete session/backend matrix remain pending.

The D3D12 debug layer was unavailable. This does not establish an FPS improvement
or detect cuts that provide neither a native reset nor a continuity break.
