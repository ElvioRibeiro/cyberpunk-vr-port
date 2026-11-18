# VR menu surface flicker

Base: fork main, 0.2.45. Investigation only.

The held-Y wheel, shop and world menu share native Ink surfaces and vr_menu.ent. Settings inside this menu redraw the ImGui controls as Ink rows.

Correlate resource generations, first repaint readiness, subsequent writes, visibility and each eye. Reproduce each panel separately before changing the common path.

The literal keyboard-F10 canvas is a separate workstream; the 0.2.45 HUD fix is already in this baseline.
