# Public API reference

Meat2D exposes a small set of installable CMake targets. Headers under
`include/meat2d/` are the supported API; `src/` is implementation detail.

## CMake targets

| Target | Purpose | Typical consumer |
| --- | --- | --- |
| `Meat2D::Core` | entities, scenes, world simulation, materials, replay, persistence, and shared utilities | every game or tool using the runtime |
| `Meat2D::Render` | camera, world-view dirty-region data, sprites, and render-facing components | graphical games and editors |
| `Meat2D::Net` | packets, reliable UDP, sessions, discovery, prediction, and snapshots | multiplayer games and dedicated servers |
| `Meat2D::Tools` | projects, templates, assets, editor-facing management, and CLI support | launchers and project tools |

The targets carry their include directories and C++20 requirement. Link the
smallest target set that satisfies an application; `Meat2D::Net` already
depends on `Meat2D::Core`, and `Meat2D::Render` uses `Meat2D::Core`.

## API areas

- `meat2d/core/`: stable IDs, fixed-step clocks, commands, and shared value
  types.
- `meat2d/scene/`: entities, components, hierarchy, tags, serialization,
  patches, and deterministic scene hashes.
- `meat2d/sim/`: bounded worlds, materials, reactions, raycasts, chunk stores,
  and deterministic or parallel stepping.
- `meat2d/render/`: camera and render data extraction. It does not require an
  SDL window in headless builds.
- `meat2d/input/`: bounded keyboard, mouse, gamepad, and touch state with
  deterministic frame edges, deltas, and action-map bindings. SDL or another
  platform event source is responsible for translating native events into this
  backend-neutral state.
- `meat2d/net/`: wire protocol, packet validation, sessions, replay-safe
  snapshots, and discovery.
- `meat2d/tools/`: project/template, asset, and editor workflows.
- `meat2d/ai/` and `meat2d/life/`: optional living simulation and agent systems.

## Compatibility rules

The public API is C++20 and versioned with the project release. Applications
must not depend on private headers, concrete source file layout, or internal
serialization details. Scene/replay formats carry versions and can reject
unsupported data; check return values and report the failure to users.

The determinism contract applies to equal configuration, state, and commands.
`World::step_parallel` guarantees repeatability for a worker configuration,
but is not required to produce the same ordering as the single-threaded
`World::step` path.

## Verification

The supported consumer smoke test is in `tests/package_consumer`. A release
must build the package, install it, configure that consumer with
`CMAKE_PREFIX_PATH`, and run the focused test suite. Sanitizer and fuzzing
presets provide additional validation for memory, undefined-behavior, and
parser boundaries.
