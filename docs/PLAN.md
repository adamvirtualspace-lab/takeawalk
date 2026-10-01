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
| 0.4 | Override `/def/photo...` photo camera: `max_distance`, `max_height`, `fly_speed`, `validation` | How far can a data mod push photo mode toward walking? |
| 0.5 | Install an `.scs` extractor and extract `/def` and `/ui` fully | Find where `walking_camera` is defined and referenced |
| 0.6 | Put `walking_camera` entries in `camera_storage.takeawalk.sii` / truck data and see what the log says | Can the engine's walking camera be selected in the world from a data mod? |

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

Language C++, built with MSVC + CMake, SCS SDK 1.15 for the plugin entry points.

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
2. Should it stay compatible with multiplayer (Convoy/TruckersMP)? If yes, data mod only.
3. ETS2 only, or ATS as well? (Same engine; mostly a packaging question.)
