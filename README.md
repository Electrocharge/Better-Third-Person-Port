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

## Modes (Mod Menu slider "Mode")
The on-screen button toggles whichever mode is selected. It starts on **mode 0**, which is FreeLook's own logic
ported 1:1, so the button behaves exactly like FreeLook's. Use it first to confirm the hooks work, then try 1 and 2.

- **0 - FreeLook (original):** skips the camera->player rotation sync, keeps the camera free, locks the head yaw
  (FreeLook hooks A, D, C, E).
- **1 - Better Third Person (game scheme):** same camera hooks as FreeLook (A, D), no head lock; also forces the
  game's own "camera relative" control scheme for the input-transform step so movement follows the camera and the
  character turns toward the movement direction.
- **2 - BTP head-only:** camera, movement and aiming stay vanilla; the head/body yaw is steered toward the real
  heading (`camera yaw - atan2(input.x, input.y)`, the game's own formula) from FreeLook's tick hooks.

## Known limits
- HEAD mode only changes how the model faces; scheme mode also turns the real player rotation, so aiming then
  follows movement instead of the crosshair (the Java mod fixes this while interacting; not implemented).
- No automatic first/third person detection: turn it off with the button in first person.
- Signatures match the same Minecraft build as FreeLook's. After a game update they need redoing.
- Mod Menu: on-screen button on/off, keybind (when the button is off), button opacity.
- The launch log prints a line the first time the transform runs while ON (original scheme + camera vector) to
  help debugging.

## Build
GitHub Actions (xmake + NDK r29, same as FreeLook) uploads `BetterThirdPerson.levipack` as a build artifact.
