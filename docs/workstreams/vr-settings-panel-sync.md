# F10 VR settings canvas synchronization

Base: fork main, 0.2.45. Fix not implemented.

ImGui publishes a canvas with its producer fence. VrPanel consumes the latest canvas on RuntimeQueue without an explicit producer-fence dependency, despite its same-queue assumption.

Confirm ordering, select completed content and retain the last valid panel or establish a measured GPU dependency. Preserve leases, pool reuse, toggles and shutdown; avoid per-frame CPU waits.

Add regressions and headset validation. Keep world Ink menus and OSR reset separate.
