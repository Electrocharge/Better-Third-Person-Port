# Better Third Person (Bedrock / LeviLaunchroid native mod)

Free-orbit third person for Minecraft Bedrock, toggled with an **on-screen button**: the camera turns
independently, movement is relative to the camera, and your character turns to face the direction you move.
Use it while in third person (back view); switch it off for first person.

## Attribution / license
Derived from **FreeLook** by **NexCaise** (2026) under its CPNC-1.0 license (`files/LICENSE`: personal /
non-commercial use, attribution required). The camera hooks, mod-menu setup and HUD-button code come from
FreeLook. This is an **unofficial derivative work**, not an official version of FreeLook, and is not affiliated
with the Java "Better Third Person" mod or its authors (no code from that mod was used, only its publicly
described behavior). Do not sell it.

## How it works (this game build)
Bedrock already contains a "camera relative" control scheme in `CameraInputTransformSystem`: it converts input
to camera-relative movement and rotates the player toward the movement direction. While the button is ON:
1. the per-entity input-transform function is hooked and the control scheme byte is forced to `1` (camera relative)
   for that call, then restored;
2. FreeLook's camera hooks stop the camera from dragging the player's rotation and keep the camera free.

## Known limits
- Aiming/attacking uses the player's rotation, which now follows movement, so it does not follow the crosshair
  like the Java mod does while interacting (not implemented).
- No automatic first/third person detection: turn it off with the button in first person.
- Signatures match the same Minecraft build as FreeLook's. After a game update they need redoing.
- Mod Menu: on-screen button on/off, keybind (when the button is off), button opacity.
- The launch log prints a line the first time the transform runs while ON (original scheme + camera vector) to
  help debugging.

## Build
GitHub Actions (xmake + NDK r29, same as FreeLook) uploads `BetterThirdPerson.levipack` as a build artifact.
