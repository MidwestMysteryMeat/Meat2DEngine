# {{PROJECT_NAME}}

{{PROJECT_NAME}} is a C++20 game built with
[Meat2D Engine](https://github.com/MidwestMysteryMeat/Meat2DEngine).

This project was created from a selectable game-type starter. The starter is
scaffolding, not a limitation: add your own entities, scenes, assets, and
systems as the game grows.

## Build and run

```bash
cmake --preset dev
cmake --build --preset dev
./build/Debug/{{PROJECT_SLUG}}
```

Use a local engine checkout while developing both projects:

```bash
cmake --preset dev -DMEAT2D_ENGINE_SOURCE=/path/to/Meat2DEngine
cmake --build --preset dev
```

Create release archives:

```bash
cmake --preset release
cmake --build --preset release
cmake --build build --config Release --target package
```

The generated archives include this game's license plus the Meat2D Engine
Apache-2.0 license and required NOTICE.

## Multiplayer hosting

The starter links Meat2D's authoritative networking API and includes baseline
hosting settings. The engine supports host/join on one computer, LAN discovery,
direct hostname/IP joins, and game-operated public directories with UDP hole
punching. Configure ports and the optional directory in `game.toml`, then map
the starter's player commands and presentation onto `AuthoritativeServer` and
`AuthoritativeClient` for the finished game protocol.

Public deployments should ship a headless server and may self-host
`meat2d_directory`. Restrictive NAT can still require a forwarded UDP port or
publicly reachable host; the directory does not relay gameplay.

## Publish

The generated GitHub Actions workflow builds and packages Windows and Linux on
every push. Tags matching `v*` also produce a GitHub release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

You can also run `meat2d publish . --repo OWNER/REPOSITORY` from the Meat2D SDK
to initialize Git, create a GitHub repository with `gh`, and push this project.

## Next steps

- Put game code under `src/`.
- Treat the selected starter as a focused starting point: side-scroller,
  top-down, metroidvania, or falling-sand.
- Use the editor's Code & Assets tab to edit source, import PNG/audio/font
  assets, and create `*.sprite.toml` frame/animation metadata.
- Change the package version when preparing releases.
- Replace this starter README and choose the final license for your project.
- Keep `CREDITS.md` and the packaged `MEAT2D_NOTICE`.
