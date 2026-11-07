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
Two modes (Mod Menu toggle "Experimental: use game camera-relative scheme"):

- **HEAD mode (default)** - built on FreeLook's tick hooks (the same place FreeLook locks the head yaw).
  Camera, movement and aiming stay vanilla. Each tick the mod reads the raw movement input (captured from the
  game's input-transform function) and computes the real heading as `camera yaw - atan2(input.x, input.y)`
  (the formula the game itself uses for its camera-relative scheme), then smoothly steers the head and body yaw
  toward it. Body yaw uses an assumed layout that is sanity-checked at runtime (head only if rejected).
- **GAME-SCHEME mode (experimental)** - forces the game's "camera relative" control scheme (value 1) for the
  input-transform step and uses FreeLook's camera hooks (skip camera->player rotation sync, camera free).

The launch log prints `BTP:` lines (hook running, button state, first few heading values) so problems can be
diagnosed from a screenshot.

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
