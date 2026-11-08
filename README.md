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

## How the button works
The on-screen button is FreeLook's button: while ON it uses FreeLook's four hooks (skip the camera->player rotation
sync, keep the camera free, lock the head yaw), so it behaves exactly like FreeLook.

Mod Menu toggle **"Better Third Person movement (experimental)"** (off by default): while the button is ON, the
game's own "camera relative" control scheme is forced for the input-transform step, so movement follows the camera
and the character turns toward the movement direction (the head lock is released so it can turn). This extra hook is
optional: if it cannot be installed on your game version, the FreeLook part still works.

## Known limits
- With movement mode on, the real player rotation follows movement, so aiming follows movement instead of the
  crosshair (the Java mod fixes this while interacting; not implemented).
- No automatic first/third person detection: turn it off with the button in first person.
- Signatures match the same Minecraft build as FreeLook's. After a game update they need redoing.
- Mod Menu: on-screen button on/off, keybind (when the button is off), button opacity.
- The launch log prints a line the first time the transform runs while ON (original scheme + camera vector) to
  help debugging.

## Build
GitHub Actions (xmake + NDK r29, same as FreeLook) uploads `BetterThirdPerson.levipack` as a build artifact.
