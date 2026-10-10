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
- v0.7 calls `489E10` every frame with a 25 m box around the walker. **Confirmed in
  game on 2026-10-06:** fences, buildings and distant ground are solid; the static
  actor count rose from about 70 to over 200 while walking and fell back afterwards.
  The user noticed a slight stutter on a low-end machine (Ryzen 5 7640U, integrated
  graphics).

## 4f. Console commands and variables

Found by static analysis for v0.9. Running commands is **confirmed in game**
(2026-10-08: the commands appear in the log and take effect). Reading a variable's live
value was wrong in v0.9 and is corrected below; the corrected layout is **not yet
confirmed**.

- The game runs its own command lines through one function, RVA 0x1E08F0, 135 callers.
  Found from the string "[cmd] '%s' - unknown command". Signature of its start:
  `40 53 48 81 EC 40 0C 00 00 48 8B D9 83 FA FF 0F 85 ?? ?? ?? ?? 48 8B 11 33 C0 41 B0 20`.
- Call shape, as in the game's own tiny `screenshot` wrapper at RVA 0x200200:
  `run(const char **text, int queue)` with `queue = -1` to run at once. The first
  argument is the address of a pointer to the text.
- A console variable is a 0x140-byte global structure in `.data`. From its start:
  name at +0x08 (0x20 bytes), default value text at +0x28 (0x65 bytes), a flag at
  +0xA1 (non-zero: the default is in effect), the set value text at +0xB1 (0x65
  bytes), a cached integer at +0x118 with its valid flag at +0x116, and at +0x120 a
  pointer to a variable that replaces this one (followed until null). Read off the
  integer getter at RVA 0x1CBD70. The plugin finds a variable by scanning the writable
  sections for its name at 8-byte alignment.
- v0.9 read the text at +0x28 and so got the default: it reported `g_adviser_alpha`
  as 0.8 when the profile had 0.42, and "restored" 0.8.
- `g_adviser_alpha 0` does not hide the route advisor.
- `g_adviser_widget_tachometer 0` hides the speedometer at once (confirmed in game);
  3 is the normal value.
- Console variables are saved with the profile when the game quits. Commands run from
  the plugin's shutdown come too late: after a session that ended on foot the profile
  had the on-foot values (tachometer 0, hints 0). The restore file is therefore kept
  when the plugin shuts down while on foot and applied at the next start.
- HUD variables in 1.61 (from the exe's strings; meanings partly from forum posts):
  `g_adviser_alpha`, `g_adviser_keep_hidden`, `g_adviser_widget_*` (tachometer,
  minimap, job_info, ...), `g_show_tutorial_hints`, `g_show_game_elements` (the
  floating world icons), `g_hud_notifications_keep_hidden`, `g_mirrors_keep_hidden`.
  There is no plain `g_adviser` any more.
- The game's HUD notifications (`hud_notification_request_t`) were not traced. The
  "walking camera" text in the dealer is a label set on one UI window, not a general
  message API, so v0.9 draws its own message window instead (`overlay.cpp`). The
  window shows over the game on the user's setup (full screen, DX11) without flicker.

## 4g. Map items, invisible barriers and collision groups

From static analysis for v0.10. The barrier bypass is **not yet confirmed in game**.

- The visitor called by `489E10` (section 4e) goes through map items overlapping the
  box and calls the item's virtual function at +0x1B8 to create its collision. The
  item's type is the byte at +0x0A (the map format's numbers: 1 terrain, 2 building,
  3 road, 4 prefab, 5 model, 0x27 bezier patch, 0x2F hookup, ...), flags at +0x38.
  The player variant (`flag = 0`, RVA 0x4A71D0) skips items with flag bits 0x2400000.
  The other variant (`flag = 1`, RVA 0x4A7290) only takes types 1, 3, 4 and 0x27
  (mask at RVA 0x1EA1B90) and is used by the developer camera, not by traffic.
- The X symbols and the invisible walls behind them are building items whose
  `building_scheme` has `player_limiter: true` ("invisible wall", "direction blocker",
  models `/model/wall/invisible.pmd`, `/dlc/dead_end_dlc.pmd`, ...). The attribute is
  the bool at +0x79 of `building_scheme_u` (from the engine's attribute table).
- The building item's collision is created at RVA 0xB026FF. It gives the static actor
  collision group 0x10 when the scheme is a player limiter, otherwise 3 or 1.
- The game's actor object (`physics_static_actor_physx_t`) keeps flags in the u32 at
  +0x90: bits 0-5 are set by RVA 0x16561A0, bits 6-11 are the collision group, set
  by RVA 0x1656210. Its PhysX actor is at +0x98. The plugin assumes the PhysX actor's
  `userData` (+0x10) points back to the game's actor and checks that by comparing
  +0x98; a ray hit whose group is 0x10 is skipped and the ray continues behind it.
- **Correction (probed in game 2026-10-10):** X barriers are static actors with flags
  ending in 0xC7, that is group 3 and type 7 (bits 0-5), not group 0x10. Ordinary
  ground and walls are 0x41 (group 1, type 1); a parked car's dynamic actor is 0x109.
  The attribute table rows put the offset before the name, so the offsets quoted
  above are one row late: `player_limiter` is the bool at +0x78 of `building_scheme_u`
  and `player_trigger` the one at +0x79. In RVA 0xB026FF `player_limiter` therefore
  gives group 3 with the type taken from the global at RVA 0x304570C, and
  `player_trigger` gives group 0x10. v0.12 ignores hits with group 3 and type 7.
  **Not yet confirmed in game.**
- v0.11's parked-car hook works (user: "almost all of the parked cars are
  colliding"), and the game exits cleanly with it. Two cars in a shop's parking bays
  had no body even with the walker next to them: either scenery models rather than
  traffic vehicles, or vehicles failing the other condition in RVA 0xB908C0
  (`flags & 0xE0000 == 0xE0000` at +0x1B8 of the vehicle's data). Not investigated.
- v0.12's barrier bypass is **confirmed in game** (2026-10-10): the walker passes
  through X barriers.
- **Signs and poles are not solid for the walker until the truck comes near.**
  Worked out by static analysis on 2026-10-10; the fix in v0.14 is **confirmed in game**
  the same day by a scripted run (see the last point).
  - The sign item class (vtable RVA 0x229A4B0, map item type 36) has an empty function
    at +0x1B8, so the item visitor of `489E10` does nothing for signs.
  - The world object keeps two lists of items which get a call every frame: the array
    at world+0x600 and the one at world+0x650. Changes to them are collected in
    pending arrays (world+0x628 and world+0x678, 16-byte entries: item, then a byte
    which is 1 to add and 0 to remove) and merged by RVA 0x47BC50. RVA 0x47BAF0 and
    RVA 0x47BA60 push a **removal** for the first and the second list (an earlier
    note here called 0x47BA60 a queue of items to build; that was wrong). Additions
    are written inline, for instance RVA 0x457430 for the second list. Item flags
    0x10 and 0x20 (+0x38) say the item is in the first or the second list.
  - RVA 0x47AF10, called from the main loop, calls the function at +0x190 of every
    item in the second list. A sign is in that list while it is shown (added by its
    function at +0x1B0, RVA 0x6E2E10; removed by its reset at +0x1C0, RVA 0x6E3020).
  - The sign's +0x190 is RVA 0x6E31A0. Every frame it asks the world for the player
    position (world virtual function +0x178, RVA 0x48B970: the object at
    world+0x31B0, then its +0x18, then that one's virtual function +0x100 gives a
    placement), measures the distance to the sign's own placement at +0x7C (RVA
    0x12FAF0) and compares it with 1225.0 (the float at RVA 0x251D25C): below, it
    creates the sign's collision object (RVA 0x486190, kept at +0xA8) and on later
    frames builds it (RVA 0x6E5F30, where the actor gets added: 6E7702 <- 6E3291 <-
    47AFCD <- 4CFDD0); at or beyond, it destroys it (RVA 0x486360). So **a sign is
    solid within 35 m of the truck**, with no memory of having been solid.
  - Sign flags (+0x38): bit 16 is set when the sign has been knocked over (contact
    callback at RVA 0x6E4D30), bit 18 has to be set and bit 22 clear for the test.
  - Compounds (class vtable RVA 0x22B7AF8, map item type 40, a group of signs and
    models stored as one item) do the same in their +0x190, RVA 0x7F8E30, with the
    same 1225.0. Their position is at the node pointed to by +0x50: three int32 in
    1/256 m, where X and Z pack the chunk in the upper 15 bits, so read as a whole
    they are simply the world coordinate times 256.
  - The world's player position is asked for in at least 24 places, so replacing it
    outright is not safe. v0.14 (`game_scenery.cpp`) hooks the two update functions
    and the position function: while one of the updates runs for an item within 25 m
    of the walker, the position function answers with the walker's position on that
    thread; everything else still gets the truck. A function which returns the
    orientation (RVA 0x48B9E0) starts with the same 35 bytes as the position function,
    so that signature runs on to the call which differs.
  - Probed near the truck, poles are static actors with flags ending 0x60 (group 1,
    type 0x20), and one was dynamic with flags 0x80.
  - The trace (`trace_collision`) still hooks RVA 0x47BA60 and logs the class, type
    and call stack of every distinct way an item is taken off the second list.
  - Test of v0.14 (2026-10-10, Svolvaer, truck parked): with `probe_key` on, P also logs
    how many of the shown signs have a collision object and O puts the walker 3 m in
    front of the nearest sign which is more than 60 m from the truck. On getting out:
    97 signs shown, 9 within 25 m of the walker all solid, 3 more within 35 m of the
    truck all solid, 85 elsewhere none solid. The sign picked by O (62 m from the truck)
    was not solid before the move; 2.6 s after it the probe hit a dynamic actor (game
    class RVA 0x2448928, flags 0x80) at 2.94 m, and the count was 6 of 6 solid near the
    walker, 12 of 12 near the truck, 0 of 97 elsewhere. Walking at it for 4 s moved the
    walker 4.6 m instead of 6 m and off the straight line: it slides round the pole, as
    it does along walls. Compounds were not part of this check.
  - The run was driven by `tools/game_input.py` (keys and relative mouse moves sent to
    the game window only, Steam's F12 screenshots to see the menus; the menu cursor
    follows relative mouse moves 1:1 at 1920x1080).
- Workshop mods that remove the X barriers do it for vehicles too. One of them is
  marked incompatible and removed from the Workshop. Ours only affects the walker.
- **Open: parked cars are not solid for the walker** although the truck collides with
  them. TM Real Walk has the same limit. What is known (probe key in game, 2026-10-09,
  plus static analysis):
  - A parked car has no physics body until the truck comes close; then it gets one
    and keeps it, and the walker collides with it too (user's observation).
  - The body is a dynamic actor, not a static one: PhysX class vtable RVA 0x2506950,
    game class vtable RVA 0x2448928, flags 0x149 (group 5). A parked car away from
    the truck returned no hit at all.
  - Traffic vehicles are a class with vtable RVA 0x2320BB8. Its virtual function 18
    (RVA 0xB8EBA0) changes the vehicle's state, stored as u32 at +0x18. It switches on
    the new state (1 to 6); states 4, 5 and 6 reach RVA 0xB8D6D0, which creates the
    dynamic actor if the vehicle has none (pointer at +0x190).
  - Not found yet: what calls that function when the truck approaches, and which
    state it asks for. No direct callers exist (it is virtual) and a search for
    constant states passed through the vtable found nothing. v0.10's trace hooks
    this function and logs each state change with its call stack.
  - A physical traffic vehicle activates map collision around itself through
    `489E10`, like the player's truck (call path via RVA 0xB6C3E8).
  - Traced in game on 2026-10-09: with the state-change hook on, a session next to a
    parked car logged only six `0 -> 1` changes (moving traffic spawning, call path
    via RVA 0xB90217). So **parked cars do not go through that function**; they are
    not this class, or get their body another way.
  - The game's dynamic actor class has its vtable at RVA 0x2448928; entry 25 (slot
    RVA 0x24489F0, function RVA 0x163F500) is its `_add_actor_to_scene`. The trace
    now hooks that as well, so whatever creates a parked car's body shows up with its
    call stack, and the probe key names the path for the car being looked at.
  - **Solved by tracing (2026-10-09).** Parked cars are traffic vehicles; their body
    is added by call paths through RVA 0xB6A50E <- 0xA2964D (inside `0xA295D0(physics
    part, on)`) <- 0xB909CD. The deciding code is the per-frame vehicle update at RVA
    0xB908C0: if the vehicle's physics is off and `0x62FD40(manager, vehicle, 625.0)`
    is true it switches it on; if it is on, the state is below 5 and
    `0x62FD40(manager, vehicle, 1225.0)` is false it switches it off.
  - `0x62FD40(manager, vehicle, distance_squared)` returns whether the vehicle, or
    anything in its chain (next pointer at +0x68), is within the distance of any object
    in two arrays of the manager (+0x210/+0x218 and +0x238/+0x240: the player's
    vehicles). Positions are placements at +0x28 (three floats, two int16 chunks). So
    a traffic vehicle is solid within 25 m of the truck and stops being solid beyond
    35 m. It has three callers.
  - v0.11 hooks the start of `0x62FD40` (its first 15 bytes are three register saves
    and can be moved to a trampoline) and also answers true when the vehicle is
    within the distance of the walker. **Not yet confirmed in game.**
  - Functions that create dynamic actors (callers of RVA 0x1651CA0): 0x6E6823,
    0x7884DD, 0x7E4D8B, 0x862134, 0x873376, 0x9002E3, 0x9D6324, 0xA1C519, 0xB69629
    (has "[traffic_vehicle]" strings), 0xB81F85, 0xB8E21B, 0xC0C0E8, 0x16CA1C8.
- The collision group test for barriers only recognises static actors: the game's
  dynamic actor class keeps its PhysX pointer elsewhere, so its group reads as unknown
  and it is treated as solid, which is what is wanted.

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
