# Take a Walk

Get out of your truck in Euro Truck Simulator 2 and walk around on foot, in first
person, with the world solid around you.

Park, set the parking brake, press **F9**, and you are standing next to the driver's
door. Walk, run, crouch and jump anywhere you like, then come back to the door and
press F9 again to get in.

**Status:** version 0.17. First-person walking is complete. Third person is planned.

## What you need

- Euro Truck Simulator 2 **1.61.1.1**, Windows, 64-bit. Tested with DirectX 11.
- Single player. It has not been tried with TruckersMP or Convoy, and it changes how
  the game behaves, so do not use it in multiplayer.

Take a Walk is a plugin, not a regular mod: it does not appear in the game's Mod
Manager and cannot be installed from the Steam Workshop. It finds the parts of the
game it needs by looking for known pieces of the game's code, so it is tied to the
game version above. After a game update it may switch features off, do nothing, or
crash the game at start, until a new release is made. If that happens, remove it.

## Install

1. Download `takeawalk-<version>.zip` from the
   [Releases](https://github.com/adamvirtualspace-lab/takeawalk/releases) page.
2. Close the game and extract the whole zip.
3. Double-click `install.bat`. It finds the game through Steam and copies the files
   into the game's `bin\win_x64\plugins` folder. If it cannot find the game it asks
   for the folder.

To install by hand, copy everything inside the zip's `plugins` folder into
`<Steam library>\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins`,
creating that `plugins` folder if the game has none.

Every time the game starts with a plugin installed it shows a notice that "advanced
SDK features" were detected. That is normal; press OK.

## Use

| Key | What it does |
|---|---|
| F9 | Get out. The truck must be standing still with the parking brake set. |
| F9 at the driver's door | Get back in. |
| Hold F9 | Go back to the cab from anywhere. |
| W A S D | Walk |
| Shift | Run |
| Ctrl | Crouch |
| Space | Jump |
| Mouse | Look around |

A small panel in the bottom left corner shows how to get back in. When you are too
far from the door for F9 alone to work, that line is greyed out and "Hold F9" is the
one that applies.

While you are on foot:

- The ground, walls, buildings, parked cars, signs and poles are solid.
- The invisible walls behind the red X symbols, which keep trucks on the map, do not
  stop you. They still stop vehicles.
- The driving keys do nothing, so walking does not rev the engine or release the
  brake. Esc, the function keys and the console still work.
- The speedometer is hidden and the driver's seat is empty, both put back when you
  get in.

## Settings

`takeawalk.ini` sits next to `takeawalk.dll` in the plugins folder. Changes take
effect the next time the game starts.

| Setting | Default | Meaning |
|---|---|---|
| `max_distance` | `0` | Furthest you can walk from the truck, in metres. 0 means no limit. |
| `head_bob` | `1` | Sway the view with each step. |
| `footsteps` | `1` | Footstep sounds. |
| `footstep_volume` | `0.2` | How loud they are, 0 to 1. |
| `hide_hud` | `1` | Hide the speedometer and tutorial hints while on foot. |
| `hide_driver` | `1` | Empty the driver's seat while on foot. |
| `key_hint` | `1` | Show the reminder panel in the bottom left corner. |
| `ignore_barriers` | `1` | Walk through the invisible walls behind the X symbols. |

The `[debug]` section holds diagnostics for development; leave them at 0.

To use your own footstep sounds, replace the `footstepNN.ogg` files in the
`takeawalk` folder next to the plugin (Ogg Vorbis, any number of them).

## Known limits

- First person only for now.
- You slide around thin poles rather than stopping dead against them, the same way
  you slide along a wall.
- The map is only solid where the game itself has collision. Off the beaten track,
  scenery that trucks can never reach may have none, and you can walk through it.
- A few parked cars in some car parks are not solid.
- The messages and the key reminder are drawn in a window on top of the game. They
  need the game to run in a window, borderless, or with Windows' full-screen
  optimisations, which is the usual case.
- If you quit the game while on foot, the speedometer stays hidden in your settings
  until the next start, when the plugin puts it back. Do not uninstall in between;
  the uninstaller checks for this.
- On slower computers there is a slight stutter while walking.

## Remove

Run `uninstall.bat` from the zip, or delete `takeawalk.dll`, `takeawalk.ini` and the
`takeawalk` folder from the game's `bin\win_x64\plugins` folder.

## If something does not work

The game's log says what the plugin did. Open
`Documents\Euro Truck Simulator 2\game.log.txt` and search for `takeawalk`.

- No `takeawalk` lines at all: the files are not in `bin\win_x64\plugins` of the
  game you are starting.
- `walking NOT available on this game build` or lines starting `unable to hook`: the
  game version is not the one this release was made for.
- F9 answers "Stop and set the parking brake before getting out": do that first.

Please include the `takeawalk` lines from the log when reporting a problem.

## Building from source

Needs Visual Studio 2022 with the C++ workload. With the game closed:

```
powershell -ExecutionPolicy Bypass -File plugin\build.ps1 -Install
```

builds `plugin\build\takeawalk.dll` and copies it, the sounds and (if missing) the
settings file into the game's plugins folder. Pass `-GameDir` if your game is not
where the script expects it.

```
powershell -ExecutionPolicy Bypass -File plugin\package.ps1
```

builds and packs `plugin\build\takeawalk-<version>.zip`, the file attached to a
release. The version is `TAKEAWALK_VERSION` in `plugin\src\takeawalk.cpp`.

| Folder | Contents |
|---|---|
| `plugin\src` | The plugin. `takeawalk.cpp` is the walking itself; the `game_*` files reach into the game. |
| `plugin\release` | Installer and notes that go into the release zip. |
| `plugin\sounds` | Footstep recordings. |
| `tools` | Scripts used to study the game: signature scanner, archive reader, skeleton reader, scripted input for testing. |
| `docs` | [RESEARCH.md](docs/RESEARCH.md): what was learned about the game and how each feature works. [PLAN.md](docs/PLAN.md): phases and what comes next. |
| repo root (`manifest.sii`, `def`) | A small data mod from the first experiment, which widens photo mode's range. The plugin does not need it. |

## Credits

- Built on the [SCS SDK](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry),
  copyright SCS Software, under its permissive licence
  (`plugin\third_party\scs_sdk\sdk_license.txt`).
- Ogg decoding by [stb_vorbis](https://github.com/nothings/stb) (public domain).
- Footsteps from Kenney's [RPG Audio](https://kenney.nl/assets/rpg-audio) pack (CC0).
- The camera approach follows what the community worked out for ETS2MobileCam.

Take a Walk is a fan project. It is not made or endorsed by SCS Software.
