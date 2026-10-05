# takeawalk
ETS2 Mod where we can walk on foot

Status: early. The data mod extends photo mode's range; the plugin only loads and
reads telemetry so far.

- [docs/PLAN.md](docs/PLAN.md): phases, repo layout, open decisions
- [docs/RESEARCH.md](docs/RESEARCH.md): how ETS2 modding works and what the game already has for walking

Target game version: ETS2 1.61.

## Building the plugin

Needs Visual Studio 2022 with the C++ workload. With the game closed:

```
powershell -File plugin\build.ps1 -Install
```

This builds `plugin\build\takeawalk.dll` and copies it to the game's
`bin\win_x64\plugins` folder. Output goes to the in-game console and `game.log.txt`.
