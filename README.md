# Better Third Person (Bedrock / LeviLaunchroid native mod)

A Bedrock port of the *idea* of the Java mod "Better Third Person": in third person the
camera orbits freely, and your character turns to **face the direction you are moving**
instead of always facing where the camera points.

- Movement stays camera-relative (same as vanilla Bedrock).
- Aiming, attacking, placing and using items still use the real look direction. The mod never
  edits your look rotation, only the head/body yaw used to draw the model.
- Mod Menu: on/off, turn speed (degrees per tick), turn body too, quick-toggle keybind (default F8).

## Attribution / license
Derived from **FreeLook** by **NexCaise** (2026), used under its CPNC-1.0 license (`files/LICENSE`,
personal / non-commercial use, attribution required). This is an **unofficial derivative work**, not
an official version of FreeLook, and is not affiliated with the Java "Better Third Person" mod or its
authors (no code from that mod was used, only its publicly described behavior). Do not sell it.

## How it works (this game build)
- Hooks the same two tick functions FreeLook uses (LocalPlayer tick, Mob tick), found by signature.
- Each tick it reads the player's horizontal velocity (`StateVectorComponent.posDelta`, offset 0x18)
  and smoothly steers the stored head yaw (`ActorHeadRotationComponent`) and body yaw
  (`MobBodyRotationComponent`) toward `atan2(-dx, dz)` while you are moving.
- Component ids are FNV-1a hashes of the component name (verified for the head component).
- The body component layout (two floats, like the head one) is an assumption. It is sanity-checked
  once at runtime and silently disabled if the values do not look like yaw angles.

## Known limits
- No automatic "third person only" detection yet: in first person it still changes how your model
  faces (only visible to other players). Use the keybind or Mod Menu toggle if that matters.
- Signatures match the same Minecraft build as FreeLook's. After a game update they need redoing.
- Not tested in-game by me.

## Build
GitHub Actions (`.github/workflows/build.yml`) builds `BetterThirdPerson.levipack` with xmake + NDK r29 like
FreeLook does, and publishes the `.levipack` as a release asset.
