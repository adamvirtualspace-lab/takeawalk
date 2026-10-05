# Take a Walk: plan

Goal: get out of the truck and walk around in first person in ETS2.

Background and evidence are in [RESEARCH.md](RESEARCH.md).

## Approach

Two deliverables, built in order:

1. **Data mod (`.scs`)**: a constrained "walk" using cameras the engine already has.
   Quick to build, survives game updates, Workshop-friendly.
2. **Plugin (`.dll`)**: real walking with ground following and collision. Much more
   work, tied to a game version, not usable in TruckersMP.

Phase 0 decides how much of phase 2 is needed at all.

## Phase 0: experiments in-game (no code)

Each one answers a question that shapes the later phases.

| # | Experiment | Question |
|---|---|---|
| 0.1 | Set `g_developer "1"` and `g_console "1"` in `config.cfg`; try free camera (`0`) | Baseline feel; confirms dev camera works on this install |
| 0.2 | Open photo mode in 1.61 and look for a walk toggle (the `.ui.photomode.walk` context) | Does 1.61 photo mode already have on-foot movement with collision? |
| 0.3 | Walk around the truck at a dealer/workshop | How does the built-in `walking_camera` feel: speed, crouch, collision? |
| 0.4 | Enable this mod (it overrides `/def/photo_camera_data.sii`: `max_distance` 40 -> 300, `fly_speed` 3.5 -> 2.0) and open photo mode | How far can a data mod push photo mode toward walking? Does `max_height` measure from the ground or the truck? |
| 0.5 | Install an `.scs` extractor and extract `/def` and `/ui` fully | Confirm the findings below against the full file tree |
| 0.6 | ~~Enable `walking_camera` in the world from data~~ | Answered from the files: no `walking_camera` def exists and the dealer scenes carry no walk settings, so it is code-driven. Outcome A is ruled out |

Outcome: a short write-up in `docs/` saying which of A/B/C below applies.

- **A**: the engine's walking camera can be enabled in the world from data. The mod
  is mostly `.scs`; the plugin is optional polish.
- **B**: only photo-mode tweaks work from data. Ship that as the "lite" mod and build
  the plugin for real walking.
- **C**: nothing useful from data. Plugin only.

## Phase 1: data mod MVP

- `manifest.sii`, `description.txt`, `icon.jpg` (276x162).
- Camera definitions from whichever phase 0 experiment worked.
- Works on every truck, including modded ones, without per-truck files if possible.
- Test: loads with a clean `game.log.txt` (no errors or warnings from this mod).

## Phase 2: plugin

Language C++, built with MSVC via `plugin/build.ps1` (Visual Studio 2022 is installed;
no CMake needed), SCS SDK 1.15 in `plugin/third_party/scs_sdk` for the entry points.

Status (2026-10-05): phase 1 tested in game: the photo camera reaches far beyond the
stock limit at walking speed, but it is still just photo mode. 2.1-2.4 work in game
(F9 steps out, WASD + mouse walk, slopes are followed). 2.5 (walls, falling) is built
as v0.5 and awaits a test. Open issue: no ground found in some places away from the truck.

| Milestone | Content |
|---|---|
| 2.1 Skeleton | DLL loads, logs to the game console, reads truck placement/speed/parking brake from telemetry |
| 2.2 Camera takeover | Locate the camera structures by pattern scan (not hard-coded offsets); toggle key places the camera beside the driver door |
| 2.3 Movement | WASD + mouse look, walk/run/crouch, eye height, head bob |
| 2.4 Ground | Follow terrain height; first try the engine's own collision/`validation` path, fall back to truck-plane height near the truck |
| 2.5 Collision | Stop at walls, vehicles, fences |
| 2.6 Rules | Only exit when stopped with parking brake on; leash distance; re-enter at the door; block driving input while on foot |
| 2.7 Polish | Footstep sounds, config file, exit/enter transition |

Risks:

- Reverse-engineered offsets break on each game update. Mitigation: signature scans,
  a version check that disables the plugin cleanly on mismatch.
- Ground and collision are the unknowns; 2.4 is the milestone most likely to stall.
- Renderer differs between runs here (`gl` vs `dx11`). Avoid a rendered overlay at
  first; use an ini file and console messages.
- Plugins are blocked in TruckersMP and may be restricted in Convoy.

## Phase 2b: third person (optional, after first person works)

Depends on phase 2.2-2.4. See RESEARCH.md section 4b.

| Milestone | Content |
|---|---|
| 2b.1 Spike | In a disassembler, find how the engine instantiates a mover / animated model actor. Go/no-go for the whole phase |
| 2b.2 Static body | Spawn a game NPC model at the player position and move it with the player |
| 2b.3 Animation | Switch between idle and walk clips, scale playback to movement speed |
| 2b.4 Camera | Orbit camera behind the character, toggle first/third person |
| 2b.5 Custom character | Own model and clips (run, crouch, jump) via SCS Blender Tools, if game NPC clips are not enough |

Start with a game NPC (`meso` skeleton, existing walk and idle clips) so no art is
needed until 2b.5. If 2b.1 fails, the fallback is first person with a body shadow only.

## Phase 2c: Convoy hangout (after 2b.2)

Goal: friends in the same Convoy session leave their trucks and see each other walking.
TruckersMP is out of scope (it blocks plugins).

Our plugin runs its own small network link between friends. It does not touch Convoy's
netcode; Convoy keeps syncing the trucks as usual. Each player sends position, facing
and state (walking, running, crouching, in truck) about 20 times a second. Receivers
smooth it between updates.

| Milestone | Content |
|---|---|
| 2c.0 Check | Everyone runs `takeawalk.dll` in a Convoy session (same game version, same mod list). Confirm the plugin loads and works there |
| 2c.1 Link | Join by session code, through direct connection or a tiny relay. Exchange player name and state |
| 2c.2 Nameplates | Draw each friend's name and a marker at their position (SPF UI + Camera API). No character model needed, so this does not wait on 2b.1 |
| 2c.3 Bodies | Show each friend with the 2b character: one more actor per friend, fed from the network |
| 2c.4 Animation | Play walk/idle/crouch from the received state, scaled to speed |
| 2c.5 Extras | Sit in a friend's passenger seat, emotes |

Depends on: phase 2 (walking) for 2c.0-2c.2, and 2b.1/2b.2 for 2c.3 onward. If 2b.1
fails, nameplates are the fallback, or our own mesh drawn through SPF's graphics hook.

Known limits:

- Parked trucks, trailers and cars have no collision for walkers (the game has
  none for them), so people walk through each other's trucks unless we add simple
  boxes ourselves.
- Every player needs the same game version and plugin version.
- Game updates can break the reverse-engineered parts for everyone at once.

## Phase 3: release

- Pack the data mod as `.scs` (zip, no `.git`, no `docs/`, no `plugin/`).
- GitHub release with the DLL and install notes.
- Optional Steam Workshop upload of the data mod via SCS Workshop Uploader.

## Repo layout

The repo root is the mod root, because the game mounts this folder directly.

```
manifest.sii          mod metadata        (phase 1)
description.txt
icon.jpg
def/                  data overrides      (phase 1)
plugin/               C++ plugin source   (phase 2; ignored by the game)
tools/                dev scripts
docs/                 research and plans
```

## Tooling still needed

- `.scs` extractor: SCS Extractor (official) or sk-zk/Extractor.
- SCS SDK 1.15.
- Visual Studio Build Tools (MSVC) + CMake, for phase 2 only.
- A disassembler (Ghidra or x64dbg) for phase 2.2 onward.

## Open decisions

1. Scope: is the constrained data-mod version enough, or is full free-roam walking
   the goal? (Decides whether phase 2 happens.)
2. ~~Should it stay compatible with multiplayer (Convoy/TruckersMP)? If yes, data mod only.~~
   Decided 2026-10-05: Convoy with the plugin is a goal (phase 2c). TruckersMP is not.
3. ETS2 only, or ATS as well? (Same engine; mostly a packaging question.)
4. Build the plugin on SPF-Framework instead of the raw SDK? Planned to try later,
   before 2.2 (see RESEARCH.md section 6).
