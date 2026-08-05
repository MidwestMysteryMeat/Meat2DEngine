# Production 8+ execution plan

This plan turns the five remaining production tracks into measurable release
gates. A feature is not considered complete merely because a type or demo
exists. Each track must have public API documentation, focused tests, a
failure policy, performance evidence, and at least one production-style sample
using the same path a project author will use.

## Scoring rule

The score is a working readiness estimate, not a marketing number. Each track
is scored across five dimensions:

| Dimension | Weight | Evidence required |
| --- | ---: | --- |
| Completeness | 25% | Documented use cases work without engine forks or private hooks |
| Reliability | 25% | Failure injection, regression tests, recovery behavior, and clean CI |
| Security/correctness | 20% | Threat model, bounded inputs, authority rules, and negative tests |
| Performance/scale | 15% | Repeatable benchmark thresholds and memory/latency budgets |
| Usability/documentation | 15% | Tutorial, API docs, sample, diagnostics, and migration notes |

A track cannot reach 8.0 if Reliability or Security/Correctness is below 8.0.
A track cannot be called production-ready while any unbounded allocation,
silent data loss, unauthenticated authority boundary, or undocumented breaking
format remains in its exit path.

## Current baseline and target

These are conservative engineering estimates, not completed acceptance scores.

| Track | Scope | Current | Target | Main reason it is below 8 |
| --- | --- | ---: | ---: | --- |
| 1 | Conventional runtime/content | 5/10 | 8+/10 | Audio, UI, fonts, effects, adapters, and full physics are incomplete |
| 2 | Editor/authoring | 4/10 | 8+/10 | Viewport, inspector, painting, editing, play-in-editor, and hot reload are incomplete |
| 3 | Multiplayer maturity | 3/10 | 8+/10 | Session crypto/auth, general replication, reconnect, late join, and relay are incomplete |
| 4 | Persistence/replay | 3/10 | 8+/10 | Complete authoritative state, migrations, streaming, and replay coverage are incomplete |
| 5 | Packaging/security/release | 3/10 | 8+/10 | Pack format, signed manifests, integrated encryption, key rotation, and release verification are incomplete |

## Track 1 — Conventional runtime/content

### 8+ exit gates

- A backend-neutral audio resource/command layer exists with streaming music,
  sound effects, groups, volume, pause/resume, device-loss recovery, and a
  real desktop adapter.

The first backend-neutral audio core is now available as `Meat2D::Audio`: it
validates bounded clip metadata and play parameters, tracks named bus gains,
emits deterministic bounded commands, and exposes an explicit device-reset
command. Decoder/device adapters, streaming buffers, spatialization, and
recovery evidence remain required for this gate.
- Runtime UI provides layout, input focus, navigation, clipping, text, fonts,
  accessibility-safe defaults, and a bounded debug console.
- Rendering has documented texture lifetime, atlas invalidation, font upload,
  camera culling, layers/parallax, color handling, and an effects/shader seam.
- Input adapters translate SDL keyboard, mouse, gamepad, and touch events into
  the existing backend-neutral action state with disconnect/reconnect tests.
- Physics has a documented deterministic boundary: either a complete engine
  solver or an explicitly optional Box2D-style adapter that cannot silently
  change authoritative simulation semantics.
- Side-scroller, top-down/RTS, RPG, and visual-novel samples use these public
  systems for a complete playable loop.

### Required evidence

- Audio device loss, missing asset, UI focus, font fallback, controller
  reconnect, resize, and renderer reset tests.
- Frame-time and memory budgets for each sample on supported desktop targets.
- A public runtime tutorial that builds a small game without editor internals.

## Track 2 — Editor/authoring

### 8+ exit gates

- The editor has a visual scene viewport, scene tree, inspector, property
  validation, tile/terrain painting, collision editing, animation editing,
  prefab overrides, undo/redo, autosave, and dependency diagnostics.
- Play-in-editor starts the same game/runtime target used outside the editor;
  it does not use a second simulation implementation.
- Hot reload has explicit supported boundaries and refuses unsafe reloads with
  a recoverable diagnostic rather than corrupting a project.
- External edits, missing assets, stale generated files, and interrupted
  background builds are visible and recoverable.
- A new contributor can create, edit, play, save, reopen, and package a sample
  without manually rewriting generated C++.

### Required evidence

- An end-to-end editor acceptance test for each broad template family.
- Deterministic scene diffs before/after every authoring operation.
- Crash/restart recovery tests for autosave and background tasks.

## Track 3 — Multiplayer maturity

### 8+ exit gates

- Handshakes negotiate protocol/build compatibility and authenticate peers
  before gameplay authority is granted.
- Session payloads use integrated authenticated/encrypted transport with key
  lifetime, replay protection, expiration, and downgrade rejection.
- Ordinary entities/components replicate incrementally with interest regions,
  baselines, interpolation, prediction, reconciliation, and late join.
- Reconnect/session resume has a bounded recovery path and a clear failure
  result when the server can no longer provide the required baseline.
- Datagram, CPU, memory, invalid-input, and bandwidth limits are enforced per
  client and exported through structured diagnostics.
- Relay fallback is authenticated, metered, lease-bound, and cannot become an
  open UDP reflector or session-originating service.

### Required evidence

- Automated loss, duplication, reordering, latency, disconnect, reconnect,
  late-join, wrong-key, wrong-version, and malicious-input tests.
- Server soak tests with fixed memory and CPU ceilings.
- Security review of the handshake, session key lifecycle, and relay role.

## Track 4 — Persistence/replay

### 8+ exit gates

- A versioned authoritative session document covers world chunks,
  scenes/entities/components, agents, organisms, projectiles, commands,
  scripts, and session metadata.
- Every subsystem has an independently bounded codec and schema version;
  unknown versions fail safely and migration is explicit.
- Generation manifests and atomic replacement recover the newest complete save
  after process kill, power loss simulation, or partial disk failure.
- Chunk streaming has explicit addressing, eviction, memory budgets, and
  deterministic procedural-generation rules.
- Replay records authoritative commands and state checkpoints, identifies the
  first divergent tick, and has cross-version fixtures.

### Required evidence

- Kill-at-every-write-point recovery tests.
- Save/load hash equality for every persisted subsystem.
- Migration, downgrade rejection, corrupt-block, decompression-bomb, and
  replay-divergence tests.

## Track 5 — Packaging/security/release

### 8+ exit gates

- A deterministic pack format sorts normalized paths, rejects traversal and
  duplicates, records per-entry sizes/hashes/codec IDs, supports random access,
  and has a versioned manifest.
- Compression is applied before encryption; already-compressed media can stay
  raw when benchmarks justify it.
- Private entries use bounded AEAD with explicit key IDs, random nonces,
  authenticated metadata, and no client-embedded master secrets.
- Manifests are signed by a build-held signing key and verified by the
  launcher/server using a pinned public key.
- Key rotation, package migration, rollback policy, interrupted writes,
  license notices, SBOMs, and artifact checksums are documented and tested.
- A clean package consumer can install and use the SDK without access to the
  source tree or build directory.

### Required evidence

- Reproducible package byte comparison from the same source manifest.
- Random-access, corrupted-entry, wrong-key, wrong-signature, rollback, and
  interrupted-package tests.
- Package size, cold-start, decompression, decryption, and memory benchmarks.
- Windows/Linux/macOS release artifacts or an explicit supported-platform
  matrix with no implied support outside it.

## Delivery order

1. Stabilize contracts and acceptance fixtures for runtime, persistence, and
   package entries.
2. Finish conventional runtime seams: input adapters, audio, fonts/UI,
   effects, and the physics boundary.
3. Build the visual editor on those public runtime contracts.
4. Complete authoritative persistence/replay and use it in editor autosave,
   package tests, and multiplayer baselines.
5. Integrate session encryption/authentication, incremental replication, late
   join, reconnect, and only then relay fallback.
6. Finish deterministic packs, signatures, key rotation, release artifacts,
   tutorials, and cross-platform consumer tests.
7. Re-score each track from evidence; do not raise a score because code was
   merely added.

## Definition of broad production readiness

Meat2D reaches the requested 8+ bar when all five tracks score at least 8.0,
the weakest Reliability and Security/Correctness dimensions are at least 8.0,
all exit-gate samples pass on every supported platform, and the compatibility
policy explicitly labels anything still experimental.

Until then, the engine should be marketed as a strong deterministic 2D
foundation and early-production toolkit for scoped projects, not as a full
Unity/Godot replacement.
