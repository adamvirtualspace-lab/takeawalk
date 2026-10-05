# Research notes

Collected 2026-10-02 against the local install: **ETS2 1.61.1.1**, Windows x64 exe,
launched with `-rdevice gl`.

Each finding is marked **[verified]** (read from the local game files or official
docs) or **[reported]** (from third-party pages, not checked here).

## 1. How ETS2 mods work

### Data mods (`.scs` / folder)

- A mod is a folder or zip (`.scs`/`.zip`) in `Documents/Euro Truck Simulator 2/mod/`.
  Its files overlay the game's virtual filesystem (`/def`, `/model`, `/sound`, ...).
  Folders are recommended for development, zips for release. **[verified, SCS wiki]**
- `manifest.sii` in the mod root holds the metadata:

  ```
  SiiNunit
  {
  mod_package : .package_name
  {
      package_version: "0.1"
      display_name: "Take a Walk"
      author: "..."
      category[]: "other"
      icon: "icon.jpg"                  # JPG, exactly 276x162
      description_file: "description.txt"  # UTF-8
      compatible_versions[]: "1.61.*"
  }
  }
  ```

  Categories: truck, trailer, interior, tuning_parts, ai_traffic, sound, paint_job,
  cargo_pack, map, ui, weather_setup, physics, graphics, models, movers, walkers,
  prefabs, other (max 2 per mod).
- Some def files are "storage" files that accept an infix so a mod can add entries
  without replacing the base file, e.g. `/def/camera/camera_storage.takeawalk.sii`.
  **[verified, SCS wiki]**
- Data mods are **declarative only**. There is no scripting; a mod cannot add new
  input handling, game logic, or a new camera behaviour, only reconfigure units the
  engine already implements.

### Plugins (`.dll`)

- The game loads native DLLs from `<game>/bin/win_x64/plugins/` (folder does not
  exist yet in this install) that export `scs_telemetry_init` / `scs_input_init`.
  **[verified: export names and `%s/plugins` are in the exe]**
- Official SCS SDK 1.15 (`scs_sdk_1_15.zip`, docs inside the archive) offers:
  - **Telemetry**: read-only truck state (world placement, speed, parking brake, ...).
  - **Input** (since SDK 1.14): the plugin can register a virtual input device.
  - Nothing for camera control, world collision queries, rendering or UI.
- Console: `sdk reinit`, `sdk unload`, `sdk reload` allow reloading a plugin without
  restarting the game. **[reported, SDK readme]**
- Anything beyond telemetry/input needs reverse-engineered game memory, which breaks
  on game updates.

## 2. What the engine already has for walking **[verified from `eurotrucks2.exe` strings and `def.scs`]**

- A `walking_camera` unit class (`walking_camera_u`) exists in the exe. No
  `walking_camera` definition was found in `def.scs`, so it is likely created in
  code or defined in another archive.
- Walk input mixes exist in every profile's `controls.sii`:
  `camwalk_for/back/left/righ` (W/S/A/D), `camwalk_run` (LShift), `camwalk_jump`
  (Space), `camwalk_crou` (LCtrl), plus `camwalk_lr/ud` look axes.
- The Steam input config lists two walk contexts: `.ui.walk` (commented
  "FIXME: Rename to .ui.truck_config.walk", i.e. the dealer/workshop walkaround) and
  **`.ui.photomode.walk`** with an action-set layer `photo_walk`. So photo mode has
  some walk-related context in 1.61. What it does in-game is **not yet checked**.
- Related UI strings: `@@walk_mode@@`, `@@notification_walking_camera@@`,
  `@@overlay_camera_walk@@`, `@@overlay_camera_crouch@@`.
- Camera units shipped in `/def/camera/units/`: behind, bumper, cabin, debug_hud,
  interior_*, top, tv, wander, wheel, window. Each truck's `accessory_truck_data`
  references its cameras (`interior_camera: camera.interior.daf.xf`,
  `debug_camera: camera.debug`, ...).
- The dealer/workshop scenes (`ui_truck_scene_config` in `/def/truck_*_scene.sii`)
  only configure an orbit camera and the showroom model; they hold no walk settings.
  Together with the missing `walking_camera` def, this means the built-in walking
  camera is set up in code and cannot be enabled in the world from a data mod.
- Photo camera definition (`photo_camera: camera.photo.basic` in
  `/def/photo_camera_data.sii`), fully moddable:

  ```
  validation: true          # collision check against the world
  validation_radius: 0.6
  max_distance: 40          # leash from the truck, metres
  max_height: 7
  fly_speed: 3.5
  ```

- Developer free camera: `g_developer 1` + `g_console 1` in `config.cfg` (both are
  currently `0` here), key `0`, moved with numpad (`dbgfwd`, `dbgback`, ...), speed
  via `g_flyspeed`. No gravity, no collision.
- SCS's stated reasons for not shipping world walking: the map has no boundaries for
  pedestrians and is not built to be seen from arbitrary spots. **[reported, SCS forum]**

## 3. Existing walking mods

| Mod | Type | How it works | Notes |
|---|---|---|---|
| TM Real Walk (IzuanBakar) | plugin DLL + ini | Hooks the game's collision; own movement, sounds, settings menu (Ctrl+F10) | 1.61, closed source. Walk/run/crouch/jump, flashlight, refuelling on foot **[reported]** |
| ETS2MobileCam (Baldywaldy09) | plugin DLL, C++/CMake/MSVC, open source | Overwrites the free camera's placement every tick via reverse-engineered `camera_manager_u`/`core_camera_u`; patches the camera tick so the engine does not overwrite it; raw mouse input | No ground/collision handling described **[reported]** |
| Roextended "Walk Around Truck" | `.scs` + edited `controls.sii`/`config_local.cfg` | Abuses eye/head-tracking presets to offset the head along fixed paths around the truck | Analogue input only, fixed paths **[reported]** |
| "Walk About Camera" (1.24 era) | `.scs` | Interior camera with widened limits | Only moves around the cab **[reported]** |

## 4. What this means

1. A pure `.scs` mod cannot deliver real walking (gravity, collision, free roam).
   It can deliver a constrained version by reconfiguring the photo camera or
   interior/debug cameras.
2. Real walking needs a plugin DLL. The hard parts, in order: taking over the camera,
   keeping feet on the ground (terrain height / collision), and surviving game updates.
3. The engine's own `walking_camera` and the photo-mode walk context are the most
   promising shortcut and are unexplored by the mods above. Worth testing before
   writing any code.

## 4b. Third-person walking

Third person needs two things: a character asset, and a way to put it in the world
under player control. The first is easy; the second is the real problem.

### The character asset

**Reuse game NPCs [verified from `def.scs`]**

- Pedestrians are "movers": skinned `.pmd` models plus `.pma` animations, defined in
  `/def/world/mover*.sii` (`mover_desc`, `mover_anim`, `mover_anim_props`,
  `mover_model_group`).
- Human animations shipped with the game include
  `/asset/animation/man/meso_walking_01.pma`, `reduced_walking_01.pma`,
  `/asset/animation/woman/meso_walking_01.pma`, `meso_walking_02.pma`, and standing
  idles such as `meso_standing_idle_01..05.pma`. I found walking and idle clips but
  **no human run, jump or crouch clips**.
- Models and animations are matched by tags because there are several skeleton
  families (`meso`, `reduced`, newer `ngn`); an animation only works on its own
  skeleton.
- Driver models also exist (`/vehicle/driver/ai_0.pmd` ... `ai_11.pmd`), posed for
  sitting.
- These assets are SCS's. Referencing them by path from a mod is fine; copying them
  into the mod is redistribution.

**Import a custom skeletal mesh [reported, SCS wiki + Steam guide]**

- SCS Blender Tools export armature + skinned mesh + actions as PIM/PIS/PIA; SCS
  Conversion Tools turn them into `.pmd`/`.pmg`/`.pma`.
- Only bone location/rotation/scale keys are exported (no shape keys, no IK at runtime).
- Limits quoted by a community guide: 255 bones, 65,535 vertices per object, 60 s per
  animation.
- A custom character lets us author the missing run/crouch/jump clips.

### Getting it into the world

| Route | How | Verdict |
|---|---|---|
| Mover on the map | Placed in the map editor, loops along a fixed path | Not player-controlled |
| Mover on a truck locator (the "animated passenger" trick) | Accessory locator hosts a mover | Glued to the truck, loops one clip. Not controllable |
| `animated_model_data` | Model + animation with `trigger_distance_sq`, `one_shot` (garage doors, tow scene) | Fixed position, trigger-only |
| Character as a drivable "vehicle" | A truck definition whose model is a person; chase camera gives third person and vehicle physics gives collision | Untested idea. No way to swap vehicles on the spot from data, and animation would not follow speed |
| Plugin spawns and drives an engine actor | Reverse-engineer how the engine creates a model/mover actor, then set its placement and animation each frame | The real solution. Hardest reverse-engineering task in the project |
| Plugin renders its own mesh | Hook the renderer and draw a skinned mesh ourselves | Lighting/shadows will not match; depends on renderer (this install has run both `gl` and `dx11`) |

Conclusion: no data-only route gives a controllable character. Third person depends
on the plugin, and on one specific unknown: creating and animating an engine actor
from plugin code. Once the plugin already owns the camera, the third-person camera
itself (orbit behind the character) is simple.

No existing walking mod I found advertises third person; TM Real Walk is first person
with a ground shadow and a hand-held flashlight. **[reported]**

## 5. Local environment

- Game: `E:\home\adam\.local\share\Steam\steamapps\common\Euro Truck Simulator 2`
  (Windows build, `bin\win_x64\eurotrucks2.exe`).
- User data: `C:\Users\user\Documents\Euro Truck Simulator 2`. This repo sits directly
  in `mod\`, so the game sees the repo folder as a mod.
- `config.cfg` says `r_device "dx11"` but the last run used `-rdevice gl`. This
  matters for any in-game overlay a plugin might draw.
- Tools present: Python 3.12, .NET, git. Missing: C++ compiler, CMake, an `.scs`
  extractor.
- `def.scs` is HashFS v2. Until an extractor is installed, `tools/scs_scan.py` can
  pull text files out of it by keyword.

## 6. Reverse-engineering tooling

Checked 2026-10-05 for phase 2.2 onward.

### REA (morluto/rea) **[reported, REA docs; not run here]**

An agent-driven front end (CLI + MCP server) over a disassembler: Hopper on macOS,
or a user-installed Ghidra. Results (decompiled functions, references, strings, call
paths) come back as structured records an agent can work from.

- Windows support is an experimental "Windows Ghidra P0": x64 host, exactly
  Ghidra 12.1.4 + JDK 21, installed by hand (`rea setup` does nothing on Windows).
  Accepts only native 64-bit non-DLL PE executables, so `eurotrucks2.exe` qualifies
  but our plugin DLL does not.
- Static only. On Windows it cannot attach to the running game, read memory or set
  breakpoints, so live offsets (`camera_manager_u` layout, placement) still need
  x64dbg or Cheat Engine.
- The Ghidra project is ephemeral and deleted on close, so a large exe may be
  re-analysed every session. Not measured.
- Does not generate byte signatures; `tools/sigscan.py` stays.
- Can compare function dossiers between two builds, which may help re-find
  signatures after a game update.

Where it could help: tracing the camera manager and free-camera tick (2.2), finding
the collision path behind the photo camera's `validation` setting (2.4/2.5), and
the mover/actor spawn spike (2b.1).

Decision: not adopted yet. Start phase 2.2 with plain Ghidra (needed either way).
If manual navigation becomes the bottleneck, try REA's Windows mode or a lighter
Ghidra MCP bridge on a copy of the exe, and confirm every finding live in x64dbg
before it goes into the plugin.

## Sources

- https://modding.scssoft.com/wiki/Documentation/Engine/Mod_manager
- https://modding.scssoft.com/wiki/Documentation/Engine/Game_data
- https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry
- https://modding.scssoft.com/wiki/Documentation/Engine/Mover_model_group
- https://modding.scssoft.com/wiki/Documentation/Tools/SCS_Blender_Tools/Animation_system
- https://steamcommunity.com/sharedfiles/filedetails/?id=2990446757
- https://github.com/Baldywaldy09/ETS2MobileCam
- https://github.com/sk-zk/Extractor
- https://www.ets2world.com/tm-real-walk-plugin-v1-0-1-61-ets2/
- https://roextended.ro/forum/viewtopic.php?t=1992
- https://forum.scssoft.com/viewtopic.php?t=353040
- https://forum.scssoft.com/viewtopic.php?t=325803
- https://steamcommunity.com/sharedfiles/filedetails/?id=2646232163
- https://github.com/morluto/rea
