# Public API reference

Meat2D exposes a small set of installable CMake targets. Headers under
`include/meat2d/` are the supported API; `src/` is implementation detail.

## CMake targets

| Target | Purpose | Typical consumer |
| --- | --- | --- |
| `Meat2D::Core` | entities, scenes, world simulation, materials, replay, persistence, and shared utilities | every game or tool using the runtime |
| `Meat2D::Render` | camera, world-view dirty-region data, sprites, and render-facing components | graphical games and editors |
| `Meat2D::Audio` | backend-neutral clip metadata, buses, bounded mixer commands, and device-reset signaling | graphical games and platform audio adapters |
| `Meat2D::Net` | packets, reliable UDP, sessions, discovery, prediction, and snapshots | multiplayer games and dedicated servers |
| `Meat2D::Tools` | projects, templates, assets, editor-facing management, and CLI support | launchers and project tools |

The targets carry their include directories and C++20 requirement. Link the
smallest target set that satisfies an application; `Meat2D::Net`,
`Meat2D::Render`, and `Meat2D::Audio` depend on `Meat2D::Core`.

## API areas

- `meat2d/core/`: stable IDs, fixed-step clocks, commands, and shared value
  types.
- `meat2d/scene/`: entities, components, hierarchy, tags, serialization,
  patches, and deterministic scene hashes.
- `meat2d/sim/`: bounded worlds, materials, reactions, raycasts, chunk stores,
  versioned bounded world snapshots, and deterministic or parallel stepping.
- `meat2d/persistence`: bounded composition of world snapshots and versioned
  scene documents with component hash verification. Agent, projectile, script,
  and transport persistence are separate follow-on codecs.
- `meat2d/render/`: camera and render data extraction plus a backend-neutral
  bounded UI context. `meat2d::ui::Context` owns widget layout, focus and
  navigation, pointer activation, checkbox state, events, and draw commands;
  it does not require an SDL window in headless builds.
- `meat2d/audio/`: bounded clip metadata, named buses, validated play options,
  deterministic per-frame commands, and explicit device-reset signaling. This
  target does not open a device or decode audio; platform adapters consume its
  command stream.
- `meat2d/input/`: bounded keyboard, mouse, gamepad, and touch state with
  deterministic frame edges, deltas, and action-map bindings. SDL or another
  platform event source is responsible for translating native events into this
  backend-neutral state.
- `meat2d/net/`: wire protocol, packet validation, sessions, replay-safe
  snapshots, and discovery.
- `meat2d/tools/`: project/template, asset, and editor workflows.
- `meat2d/ai/` and `meat2d/life/`: optional living simulation and agent systems.
- `meat2d/c_api.h`: versioned exception-free C ABI for the bounded World subset;
  see [C ABI](C_API.md).
- `meat2d/compression/`: bounded raw/LZAV blocks with versioned envelopes,
  checksums, and decompression limits; see [Compression and packed assets](COMPRESSION_PLAN.md).
- Encryption and key-management policy is documented separately in
  [Encryption and key management](ENCRYPTION_PLAN.md); no cryptographic API is
  enabled until its key-ownership and dependency review is complete.
- `meat2d/security/`: optional libsodium XChaCha20-Poly1305-IETF blocks with
  bounded authenticated encryption; enable with `MEAT2D_ENABLE_SODIUM` after
  supplying a reviewed libsodium installation.
- `meat2d/tools/AssetPack.hpp`: deterministic normalized-path packs with
  bounded compression and optional explicit per-entry encryption.

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
