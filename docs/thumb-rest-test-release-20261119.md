# 0.2.45 Thumb-rest test 1

Test update for CyberpunkVR Port 0.2.45 on Cyberpunk 2077 2.5.
**Requires an existing complete 0.2.45 installation.** This is a DLL-only update.

## Changes

- Adds optional thumb-rest touch as L3/R3 alongside physical stick clicks, off by default.
- Adds **Controls > META QUEST**, visible only when a Quest 2/3S/3 launcher selection
  agrees with a supported Quest system identity and both active Oculus Touch profiles
  reported by OpenXR. Unknown, failed or incompatible reports hide the page and block touch.
- Revokes touch on failed/unfocused synchronization; retains existing click/chord routing.
- Explains in the menu that this mapping is provisional until control rebinding and
  fine tuning are implemented.

OpenXR reports are not hardware attestation. A runtime masking both system identity
and controller profiles can still appear as Quest/Touch. The launcher remains a
required additional check.

This branch is based on 0.2.45. Separate experimental VRS, OSR reset and F10 patches
are not included.

## Install and rollback

1. Close the game.
2. Back up `red4ext/plugins/cyberpunkvrport/cyberpunkvrport.dll` outside the plugin folder.
3. Copy the ZIP's `red4ext` directory into the game root and replace the DLL.
4. Select the correct headset in the launcher. On a supported Quest setup, enable
   **Controls > META QUEST > Thumb rests as L3 / R3**.

Restore the original DLL to roll back. The package contains no settings, calibration,
assets or dependency replacements. The README, manifest and test logs are reference files.

## Validation

Release x64 plugin build/link and `thumbrest_input` / `scanner_input` passed.
The user confirmed the original thumb-rest mapping working in the headset.
**The new OpenXR gate and conditional META QUEST page still need in-game validation.**

Check each touch/click, overlapping holds, scanner/D-pad ownership, both-rest menu hold,
disable while touching, restart persistence and focus loss. On a non-Quest setup,
including a deliberately wrong Quest launcher selection, confirm the page stays hidden
and physical controls retain their usual behavior. Collect the OpenXR system/profile
lines from `cyberpunkvrport.log` if a valid Quest setup does not show the page.
