# Meat2D parity plan

## Objective

Meat2D is intended to become a general-purpose C++20-first 2D engine for
side-scrollers, top-down games, RTS, RPGs, visual novels, metroidvanias,
shooters, sandboxes, cellular roguelites, and destructible-terrain games. It
should provide selectable game-type templates,
similar to the starter/project workflows of larger engines.

The project should not attempt to reproduce every feature of a general-purpose
3D engine. Its differentiator is deterministic cellular simulation and
authoritative destructible worlds. The parity target is the surrounding game
creation experience provided by mature open-source 2D engines.

## Baseline

The existing engine already provides:

- deterministic fixed-step cellular simulation with fixed-point material state;
- chunked worlds, dirty-region rendering data, raycasts, projectiles, AI, and
  organism simulation;
- authoritative UDP sessions, reliability, prediction/reconciliation,
  snapshots, interest management, discovery, and hole punching;
- CMake SDK targets, templates, a project browser/launcher, replay inspection,
  persistence for fixed-size worlds, tests, and benchmarks.

The cellular simulation is one optional gameplay system and the falling-sand
starter is one template. It must not define the product boundary.

The primary gaps are the general gameplay runtime, conventional 2D rendering
and content systems, visual scene authoring, scripting, complete authoritative
persistence/replay, network security/relay support, and release automation.

## Completion definition

Meat2D reaches the target when a new user can install the SDK, create a project
from a template, author a scene, add input/physics/audio/UI/scripts, run it
locally and over the network, save and replay the complete authoritative state,
and package it for every supported desktop platform without writing engine
infrastructure first.

## Implementation order

### Phase 0 — Reliable SDK foundation

Status: **in progress**

- [x] Add a headless preset that does not require SDL.
- [x] Add selectable AddressSanitizer/UndefinedBehaviorSanitizer/ThreadSanitizer
      CMake configuration.
- [x] Add CTest timeout protection.
- [ ] Add GitHub Actions for Linux GCC/Clang, Windows MSVC, sanitizer builds,
      package installation, and package-consumer validation.
- [ ] Add fuzz targets for packet, fragmentation, replay, sprite metadata, and
      project-path parsing.
- [ ] Add release tags, changelog, migration notes, generated API docs,
      checksums, and an SBOM.
- [x] Correct product positioning and expose independent genre selectors.
- [ ] Cover side-scroller/action-platformer, top-down/RTS, RPG, visual novel,
      destructible artillery, cellular roguelite, falling-sand, and
      sandbox-survival starters.

Acceptance gate: a clean checkout builds, tests, installs, packages, and is
consumed by `tests/package_consumer` on supported CI platforms.

### Phase 1 — General gameplay runtime

Status: **in progress**

- [x] Stable entity IDs and lifecycle.
- [x] Scene/entity/component runtime separate from cellular terrain.
- [x] Initial transform, sprite, and collider components.
- [x] Versioned scene serialization and deterministic scene hashing.
- [x] Basic transformed collider queries and sensor filtering.
- [x] Integer camera transforms and viewport clamping.
- [x] Fixed-tick sprite animation playback.
- [x] Deterministic axis-separated kinematic movement with anti-tunneling steps.
- [x] Initial rigid-body component with gravity, acceleration, limits, and
      collision response.
- [x] Collision category/mask filtering.
- [x] Ordered scene lifecycle/component/tag events and deterministic subtree
      duplication for prefab-style composition.
- [x] Explicit gameplay-group aliases and stable sprite render-layer queries.
- [x] Named scene registry/stack with deterministic replace, push, and pop flow.
- [x] Bounded snapshot-backed scene undo/redo with branch invalidation.
- [x] Deterministic entity-level scene diffs for editor and replication seams.
- [x] Bounded hash-checked scene snapshots for autosave, prefabs, and transport.
- [x] Cross-scene prefab/template subtree instantiation with fresh IDs and
      preserved local data.
- [x] Reusable bounded fixed-timestep accumulator with interpolation alpha.
- [ ] Full rigid-body solver, audio, UI,
      and script components.
- [ ] Editor-managed prefab overrides.
- [x] Fixed simulation tick accumulator with interpolated rendering contract.
- [ ] Deterministic serialization for all networked components.

Acceptance gate: a top-down and a side-scroller sample can be built without
each sample implementing its own entity and scene framework.

### Phase 2 — Rendering and content systems

Status: planned

- [ ] Texture/atlas cache and sprite batching.
- [x] Backend-neutral keyboard/mouse input state and action bindings.
- [ ] SDL/gamepad/touch adapters, culling, layers, parallax, and screen effects.
- [x] Sprite animation playback.
- [x] Bounded deterministic particle simulation.
- [x] Renderer-neutral bounded debug draw command list.
- [ ] Fonts and backend adapters for debug drawing.
- [ ] Keyboard, mouse, gamepad, and touch input with action maps and rebinding.
- [ ] Audio playback, music, mixing, and spatial audio.
- [ ] Runtime UI and debug console.
- [x] Initial renderer-neutral tilemap layers, atlas metadata, solid-cell
      queries, deterministic hashing, and versioned serialization.
- [ ] Optional Box2D-backed rigid-body physics while preserving the deterministic
      terrain simulation as the authoritative substrate.

Acceptance gate: the sample games include animation, camera movement, input,
collision, audio, UI, and particles using public Meat2D APIs.

### Phase 3 — Visual authoring

Status: planned

- [ ] Scene tree and visual viewport.
- [ ] Component inspector and property editing.
- [ ] Drag-and-drop asset import and terrain/tilemap painting.
- [x] Prefab/template subtree instantiation; editor-managed overrides remain.
- [x] Undo/redo, bounded autosave snapshots, and deterministic scene diffs.
- [ ] Collision-shape and animation editors.
- [ ] Play-in-editor, hot reload, and live network-session testing.

Acceptance gate: a small game can be assembled, played, saved, and reopened in
the editor without manually editing generated engine code.

### Phase 4 — Scripting and language surface

Status: planned

- [ ] Stable C++20 API remains the primary low-level interface.
- [ ] Versioned C ABI for bindings.
- [ ] Sandboxed Lua gameplay/mod API.
- [ ] Deterministic RNG and command-only authoritative mutations.
- [ ] Script budgets, serialization, replay tests, and API documentation.
- [ ] Rust/C# bindings after the C ABI is stable.

Acceptance gate: a scripted sample has identical authoritative hashes across
server, replay, and supported client configurations.

### Phase 5 — Multiplayer maturity

Status: planned

- [ ] Replicate general entities/components, not only terrain-oriented state.
- [ ] Late join, baseline snapshots, reconnection, and session resume.
- [ ] Entity interest management, interpolation, and lag simulation.
- [ ] Authenticated/encrypted sessions and protocol version negotiation.
- [ ] Rate limits, packet budgets, abuse controls, and observability.
- [ ] Authenticated relay fallback for restrictive NAT.

Acceptance gate: packet loss, reordering, latency, reconnects, and direct-to-
relay fallback are covered by automated integration tests.

### Phase 6 — Complete persistence and replay

Status: planned

- [ ] Persist agents, organisms, entities/components, projectiles, scripts, and
      session metadata.
- [ ] Crash-safe atomic saves and incremental snapshots.
- [ ] Chunk streaming/unbounded-world addressing with explicit policy.
- [ ] Save migrations and schema versioning.
- [ ] Full authoritative command/state replay and replay visualization.
- [ ] Cross-version compatibility tests.

Acceptance gate: a saved or replayed session reconstructs the complete
authoritative state, not only the cellular World.

### Phase 7 — Ecosystem and release

Status: planned

- [ ] Beginner tutorial and one-hour game guide.
- [ ] Multiplayer, platformer, destructible-terrain, cellular-roguelite,
      sandbox-survival, RPG, and modding samples.
- [ ] API reference, architecture diagrams, security policy, and contribution
      templates.
- [ ] Windows/Linux/macOS packages, package-manager recipes, and binary SDKs.
- [ ] Stable release cadence and documented compatibility policy.

Acceptance gate: a first-time contributor can build, test, document, and ship a
sample using the documented workflow.

## Immediate work queue

1. Merge and validate the Phase 0 CMake changes.
2. Add CI and package-consumer validation.
3. Split the monolithic simulation test into subsystem targets while retaining
   the deterministic aggregate test.
4. Design the entity/component serialization contract before implementing the
   editor or network replication layer.
5. Resolve streamed-world addressing before expanding persistence and interest
   management.
