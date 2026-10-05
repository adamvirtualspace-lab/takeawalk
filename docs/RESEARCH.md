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
- Plugins are also loaded from paths listed under
  `HKLM\SOFTWARE\SCS Software\Euro Truck Simulator 2\Plugins`. All SDK calls happen
  on the main thread only. The SDK headers are MIT-style licensed (`sdk_license.txt`),
  so they can be vendored and redistributed. **[verified, `plugin/third_party/scs_sdk`]**

### How open is ETS2 modding, layer by layer

Checked 2026-10-05 against the SCS modding wiki and the vendored SDK.

| Layer | Official support | Openness |
|---|---|---|
| Assets (models, textures, animations) | SCS Blender Tools + Conversion Tools, documented formats | Open. Blender Tools are open source |
| Definitions (`.sii` units: trucks, cameras, economy, ...) | Documented on the wiki, overlay through the mod manager | Open, but limited to units the engine already implements |
| Map | Official Map Editor | Open for map mods |
| Sound | FMOD Studio project template | Open |
| Archives | Game Archive Extractor, Workshop Uploader | Open |
| Game logic / scripting | None. No scripting language exists | Closed |
| Plugin API (SDK) | Telemetry (read) + input devices (write) | Narrow: no camera, world, physics, rendering or UI access |
| Everything else (camera, collision, actors, UI) | None | Reverse engineering only |

**[reported, SCS modding wiki: documentation index lists tools, engine docs and
the Telemetry SDK, and nothing on scripting or a wider plugin API]**

So SCS is open for content and closed for behaviour. The SDK is deliberately small.
Community frameworks (section 6, SPF-Framework) fill the gap by reverse engineering, the way SKSE and
CommonLib do for Skyrim, but on a much smaller scale.

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
| TM Real Walk (IzuanBakar) | plugin DLL + ini | Hooks the game's collision; own movement, sounds, settings menu (Ctrl+F10) | 1.61, closed source. Walk/run/crouch/jump, flashlight, refuelling on foot **[reported]**. See notes below |
| ETS2MobileCam (Baldywaldy09) | plugin DLL, C++/CMake/MSVC, open source | Overwrites the free camera's placement every tick via reverse-engineered `camera_manager_u`/`core_camera_u`; patches the camera tick so the engine does not overwrite it; raw mouse input | No ground/collision handling described **[reported]** |
| Roextended "Walk Around Truck" | `.scs` + edited `controls.sii`/`config_local.cfg` | Abuses eye/head-tracking presets to offset the head along fixed paths around the truck | Analogue input only, fixed paths **[reported]** |
| "Walk About Camera" (1.24 era) | `.scs` | Interior camera with widened limits | Only moves around the cab **[reported]** |
| SPF_CabinWalk (TrackAndTruckDevs) | SPF-Framework plugin, open source | Animates the interior camera's seat position and head rotation through SPF's Camera API; one hook on the camera-from-input update | Cabin only: driver seat, passenger seat, standing spot, sofa. Walk forward/back in a fixed area, head bob, crouch. Only leaves the seat when stopped with the parking brake on **[reported, source read]** |

### TM Real Walk in detail **[reported, Gumroad product page, v1.0.0 Beta 4.5, read 2026-10-05]**

Closed source, licensed per PC with an online key check. There's a free 7-day trial;
the full version is paid. Windows only, single-player only, ETS2 and ATS 1.61.
Hints about how it works, from its own description and changelog:

- **Needs `g_developer 1` and `g_console 1`**, so it probably builds on the developer
  free camera, as ETS2MobileCam and our `game_camera.cpp` do. (Inference.)
- **Ground and collision come from the game's own collision scene**, which it finds
  by scanning memory: Beta 4.5 fixed a bug where that scan touched all of the game's
  reserved memory and cost up to ~10 GB of RAM. Lesson for 2.4: find the collision
  scene through a signature or global pointer, or scan committed memory only.
- **Limits of the game's collision**: parked cars, trailers and furniture inside
  buildings have none, so you walk through them. Stairs need "hold E", and F8 hops
  through a wall when stuck. Ground following has gaps even for them.
- Traffic can knock the walker over, so it reads traffic vehicle positions.
- Other features: trailer coupling on foot, fuelling with a pay-at-pump animation,
  flashlight, ground shadow, TrackIR, a 16-language settings menu.
- First person only. No third person.

We don't reverse engineer their DLL: it's licensed, closed code. Playing the trial to
compare how walking feels is fine.

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
| Character as a drivable "vehicle" | A truck definition whose model is a person; chase camera gives third person and vehicle physics gives collision | Rejected 2026-10-05. Swapping vehicles sends your truck to a garage, so it cannot stay parked beside you; animation would not follow speed |
| Plugin spawns and drives an engine actor | Reverse-engineer how the engine creates a model/mover actor, then set its placement and animation each frame | The real solution. Hardest reverse-engineering task in the project |
| Plugin renders its own mesh | Hook the renderer and draw a skinned mesh ourselves | Lighting/shadows will not match; depends on renderer (this install has run both `gl` and `dx11`) |

Conclusion: no data-only route gives a controllable character. Third person depends
on the plugin, and on one specific unknown: creating and animating an engine actor
from plugin code. Once the plugin already owns the camera, the third-person camera
itself (orbit behind the character) is simple.

No existing walking mod I found advertises third person; TM Real Walk is first person
with a ground shadow and a hand-held flashlight. **[reported]**

## 4c. Camera internals used by the plugin **[verified in game on 1.61.1.1, 2026-10-05]**

Starting point was the layout documented by ETS2MobileCam for 1.58; the values below
are the ones confirmed to work on 1.61.1.1. `tools/sigscan.py` checks the signatures
against an exe on disk.

- Camera manager pointer: signature `48 8B 05 ?? ?? ?? ?? 41 FF CE` (one match, RVA
  0x741D0F). The RIP-relative operand is a global holding `camera_manager_u *`.
- Free camera update function: signature
  `40 53 48 83 EC ?? 48 83 B9 ?? ?? ?? ?? 00 48 8B D9 0F 29 74 24` (one match, RVA
  0x54FAD0). Overwriting its first byte with `C3` (ret) stops the engine from moving
  the free camera, so placements written by the plugin stick.
- `camera_manager_u`: current camera index u32 at 0x10, requested camera index u32 at
  0x14 (0x0E = none; writing an index switches camera), camera pointer array at 0x30
  (items 0x38, count 0x40; 14 cameras), index 0 is the free camera.
- `core_camera_u`: FOV float at 0x20, placement at 0x40: `float position[3]`,
  `int16 chunk_x`, `int16 chunk_z`, `float rotation[4]` (quaternion w, x, y, z).
- Coordinates: world X/Z = chunk * 512 + offset, with the offset centred on the chunk
  (range -256..256). Y is plain world height. Checked against telemetry: camera
  (149.73, -124.80) in chunk (26, -195) next to a truck at world (13454.8, -99956.0).
- Orientation: yaw around +Y equals telemetry heading * 2*pi (0 = north = -Z,
  counter-clockwise), pitch around +X is positive upward; quaternion = yaw * pitch.
- Writing `requested = 0` works without `g_developer`.

## 4d. Physics internals used for ground following

Found by static analysis of the 1.61.1.1 exe with `tools/exe_analysis.py` (needs the
`capstone` and `numpy` Python packages), then **confirmed in game on 2026-10-05**:
the raycast call works and slopes are followed.

- The game links **PhysX 3.4** statically (source paths
  `...\physx\version_patched\PhysX_3.4\...` are in the exe). It is built without RTTI,
  so classes cannot be found through type information.
- `NpSceneQueries::raycast` is at RVA 0x1B04B90. It was identified as the virtual
  function just before the one referencing the string " Precise sweep doesn't support
  MTD..." (`sweep`) in the `NpScene` vtable (vtable RVA 0x2500748, raycast is entry 272).
  `NpVolumeCache::raycast` (RVA 0x1B0C860) starts with the same 76 bytes, so the
  signature has to be longer than that.
- Arguments: `this`, `const PxVec3 *origin`, `const PxVec3 *unitDir`, `float distance`,
  `PxRaycastCallback *`, `const PxHitFlags *` (by address, because `PxFlags` has a copy
  constructor), `const PxQueryFilterData *`, `PxQueryFilterCallback *`, `const PxQueryCache *`.
- `NpPhysics::mInstance` is the global at RVA 0x3046FC0, found from
  `NpPhysics::createInstance` (the function referencing "Scale invalid."). Signature
  for one instruction reading it: `48 8B 3D ?? ?? ?? ?? 48 8B 4B 08 48 81 C7 A0 00 00 00`.
- `NpPhysics`: scene array pointer at +0x08, scene count (u32) at +0x10, seen in
  `NpPhysics::createScene`.
- There is one scene. Ground is in it as static geometry that rays can hit, and the
  truck as dynamic geometry. Heights are plain world heights (ground under the truck
  3.97, truck origin 3.98, so the truck's origin is at ground level).
- Scene coordinates are world coordinates minus a whole number of 512 m chunks. In the
  one session measured the origin was the chunk the truck was in, (26, -195). The
  plugin finds it by looking for the truck with dynamic-only rays and re-checks every
  2 s, so it does not depend on that rule.
- The walk keys are hidden from the game by subclassing its window procedure; the
  user confirmed the engine does not rev while walking.

## 4e. Collision only exists around vehicles

Measured in game with a diagnostic build (v0.6), then traced in the exe.

- The PhysX scene held only about 70 static actors in a town. Fences, buildings and
  ground more than 10-25 m from the truck were not in it, so rays passed through them.
- Static actors are created on demand. Call path of a creation at run time (exe RVAs):
  main loop `426360` -> `4260FE` -> `4CF830` -> player vehicle update `5D86DB` ->
  `489E10` -> item visitor (`13CAE98`, virtual `4A71D0`) -> `7315BB` (creates the
  static actor) -> `1655070` (scene add) -> `physics_static_actor_physx_t::_add_actor_to_scene`
  (`1639EE0`, vtable slot RVA 0x2448848; `_remove_actor_from_scene` is the next slot).
- `489E10(world, box, flag)` creates the collision of the map items inside a box. The
  vehicle update builds the box with `46D4C0(unused, box_out, position, size)`:
  `position` is three floats plus two int16 chunk indices, `size` three floats. The box
  is square with half side `clamp((max(size.x, size.z) + 1) / 2, 0.01, 200) + 0.125` m
  and at least 50 m half height; it is 40 bytes. For the truck that is only about a
  metre beyond its own bounding box.
- `world` is the global at RVA 0x36AE6D8. One signature gives all three addresses:
  `E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8D 54 24 58 45 33 C0 E8 ?? ?? ?? ?? 48 8B 8E`
  (call make box, load world, call activate), one match at RVA 0x5D879E.
- `489E10` has eight callers, so other vehicles do the same. Collision nothing asks
  for disappears again (the actor count dropped from 105 to 69 after loading); how was
  not traced, the removal did not go through the vtable slot that was watched.
- v0.7 calls `489E10` every frame with a 25 m box around the walker. **Not yet
  confirmed in game.**

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

### SPF-Framework (TrackAndTruckDevs) **[reported, repo docs and source; not run here]**

An open-source (Apache-2.0) C++ plugin framework for ETS2/ATS, still active (last
commit 2026-10-03). The closest thing ETS2 has to SKSE + CommonLib. One
`spf-framework.dll` goes in `bin/win_x64/plugins`; plugins load through it with a
stable C API. Features relevant to us:

- **Camera API**: switch cameras; read and write the free camera's position,
  orientation and FOV; interior camera seat position, head rotation and limits;
  behind and top cameras including their `validation` (collision) settings.
  This covers most of milestone 2.2.
- **Hooks API**: signature hooking through MinHook. Signatures support ranges and
  optional bytes (`40 [0-1?] 56 48 [81-83] ec`), so one pattern can match several
  game builds. Also has string, constant, vtable and backward searches.
- **Reflection API**: `Reflection_GetAttributeOffset("vehicle_interior_camera",
  "head_offset")` resolves a field offset **by name** from the engine's own unit
  descriptors (the same names as in `.sii` files). This could replace hard-coded
  offsets like `CAMERA_PLACEMENT = 0x40` in `plugin/src/game_camera.cpp` and survive
  game updates.
- Also: Vehicle API (player and traffic vehicles), GameWorld API (time, cities),
  virtual input, console commands, keybinds, and a Dear ImGui UI on DX11, DX12
  and OpenGL.
- Not provided: ground height, collision queries, spawning actors. Milestones 2.4,
  2.5 and 2b are still ours to reverse engineer.

### x64dbg Prism3D Unit Resolver (Baldywaldy09) **[reported, repo only]**

An x64dbg plugin for Prism3D, the ETS2 engine, by the author of ETS2MobileCam.
Judging by its name and screenshot, it labels engine unit objects while debugging.
It would help with the live half of reverse engineering that REA cannot do.

### What this changes

Building `takeawalk.dll` on SPF instead of the raw SDK would give us camera control,
hooking, name-based offsets, keybinds and a settings UI. Our own work would be
ground, collision and movement. The cost is a dependency: players must install SPF,
and our plugin breaks if SPF lags a game update. Decide before starting 2.2.

## 7. Techniques from crossover mods

Checked 2026-10-05: two mods that mix whole games, for ideas that carry over.

### SkyCraft: Minecraft in Skyrim **[reported, repo source read]**

Both real games run at once. A Skyrim SKSE plugin (C++, CommonLibSSE-NG) and a
Minecraft Fabric mod exchange state through shared memory. Minecraft runs hidden and
owns player physics; Skyrim draws everything.

- **Collision**: the plugin reads Skyrim's Havok world around the player and turns
  it into 1/8-block voxels for Minecraft's collision. Their design doc describes two
  stages: (A) ray casts on a grid around the player, spread over frames; (C) read
  the actual collision shapes.
- **Puppet player**: Skyrim's own movement is switched off and the player is moved
  to Minecraft's position each frame, so NPC AI, triggers and quests still see it.
- **Camera**: one hook on `PlayerCamera::Update` overwrites the view each frame.
- **Testing**: stand-in scripts (`fake_skyrim.py`, `fake_guest.py`) test each half
  without the other game.
- Possible only because Skyrim's community has mapped the engine (CommonLib's named
  classes and Address Library IDs). ETS2 has no equivalent at that depth.

### 2010 Rust Rewrite Mashup: MW2 + Skate 3 + Minecraft **[reported, repo source read]**

No game is modded. MW2 runs on IW4L (a from-scratch Rust rewrite reading the
original files), Skate 3's physics and animation come from a Rust reimplementation,
and the Minecraft world comes from MinecraftOSS. All three are libraries in one
process.

- **Collision handoff**: the MW2 map's collision around the player is given to the
  skate physics and rebuilt as the skater moves.
- **Generated gameplay data** (`skate/rails.rs`): MW2 maps have no grind rails, so
  rails are found by probing collision. A walkable face edge counts as a lip when
  the ground falls away past it and nothing rises there.
- **Retargeting** (`skate/rig.rs`): Skate 3 bones are mapped onto the MW2 soldier
  skeleton.
- **No game files shipped**: a converter extracts what it needs from the player's
  own `default.xex` on first run.
- Not an option for ETS2: there is no open rewrite of the engine.

### Ideas for this project

1. **2.4 Ground**: SkyCraft's stage A. Find the call behind the cameras'
   `validation` check and cast rays on a grid around the walker instead of building
   our own world model.
2. **2.5 / 2.6 Walkable areas**: SCS's objection is that the map has no pedestrian
   boundaries. Derive them from collision the way `rails.rs` derives rails: slope
   limit, step height, drop-offs.
3. **2.6 Puppet**: switch off the normal control, write the camera every frame, keep
   the engine's own objects in place (what `game_camera.cpp` already does).
4. **Testing**: a stand-in that replays recorded telemetry, so movement code can be
   tested without restarting the game.
5. **2b**: retarget clips from another skeleton onto the `meso` NPC skeleton for
   the missing run/jump/crouch, once 2b.1 shows an actor can be controlled.

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
- https://modding.scssoft.com/wiki/Documentation
- https://github.com/TrackAndTruckDevs/SPF-Framework
- https://github.com/TrackAndTruckDevs/SPF_CabinWalk
- https://github.com/Baldywaldy09/x64dbgPrism3DUnitResolver
- https://github.com/US3R190/SkyCraft-chasm-
- https://github.com/chasmlol/2010-rust-rewrite-mashup
- https://izuanbakar.gumroad.com/l/tmrealwalk
