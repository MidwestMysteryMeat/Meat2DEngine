# Production-readiness plan

## Current position

Meat2D is a strong deterministic simulation/networking foundation with an
installable C++20 SDK, focused templates, editor scaffolding, release
automation, fuzz targets, coverage reporting, and benchmark gates. It is ready
for scoped projects and technical prototypes. It is not yet a general-purpose
engine parity release because several user-facing and multiplayer guarantees
are still incomplete.

The parity checklist currently contains 89 tracked items: 49 complete and 40
open. The checklist number is useful for progress, but the exit gates below
are the actual release criteria.

## Release tiers

| Tier | Meaning | Required gates |
| --- | --- | --- |
| Foundation-ready | Safe to build experiments and scoped deterministic games | clean builds, strict tests, sanitizers, fuzz targets, package consumer, release metadata |
| Early-production | Safe for a shipped single-player or bounded multiplayer game with explicit limitations | complete save/replay contract for the game, stable C++ API, crash-safe saves, content pipeline, threat-model review, supported platform package |
| Parity-ready | Broad engine suitable for the requested genre families | general entities/components, audio/UI, editor authoring, secure multiplayer, scripting boundary, complete persistence/replay, compatibility policy, samples for every template |

Current assessment: Foundation-ready, approaching Early-production for scoped
games, not Parity-ready.

## Phase status snapshot

- Phase 0 — SDK foundation: **100%**
- Phase 1 — gameplay runtime: **91%**
- Phase 2 — rendering/content: **57%**
- Phase 3 — editor/authoring: **38%**
- Phase 4 — scripting/API: **42%**
- Phase 5 — multiplayer maturity: **14%**
- Phase 6 — persistence/replay: **17%**
- Phase 7 — ecosystem/release: **11%**

These percentages describe the current parity checklist and are not claims
that a phase is production-complete. In particular, Phase 0 is the strongest
because its build, test, packaging, fuzzing, coverage, benchmark, and release
gates are implemented; later phases contain larger feature and integration
gaps.

## Current production blockers

1. Full audio, UI, fonts, shaders, platform input adapters, and visual effects.
2. Complete editor workflow: viewport, inspector, tile painting,
   animation/collision editing, and hot reload.
3. Secure multiplayer: encryption, authentication, protocol negotiation,
   reconnects, rate limits, entity replication, and relay fallback.
4. Complete persistence/replay for scenes, entities, agents, projectiles,
   scripts, and session state.
5. A versioned C ABI plus sandboxed Lua/mod support.
6. Incremental replication, interpolation, late join, and streamed/unbounded
   worlds.
7. Production samples for each template family, plus macOS and package-manager
   support.
8. Tutorials, security policy, compatibility policy, crash-reporting guidance,
   and a predictable release cadence.

The engine is production-capable for scoped deterministic 2D projects,
templates, simulations, and early multiplayer prototypes. It should not yet
be presented as a production/parity-ready Unity- or Godot-class general engine.

## Execution order

### Track A — Authoritative state and recovery

1. Define a versioned authoritative session document containing world chunks,
   scenes/entities/components, agents/organisms, projectiles, commands, and
   session metadata.
2. Keep each subsystem codec bounded and independently testable; do not create
   one monolithic save file implementation.
3. Add atomic generation manifests so a load can choose the newest complete
   generation after a process interruption.
4. Add schema-version rejection and migration hooks before adding new state.
5. Extend replay from World paint events to authoritative commands and state
   checkpoints, then add cross-version fixtures.

Exit gate: a killed/interrupted save restores the last complete generation;
agents, entities, and session metadata survive reload; replay reproduces the
same authoritative hashes and reports the first divergent tick.

### Track B — Multiplayer trust and scale

1. Version and authenticate the handshake, then add authenticated/encrypted
   payloads with explicit key/session lifetimes.
2. Add rate limits, packet budgets, malformed-input counters, and structured
   security diagnostics.
3. Replicate ordinary entities incrementally with interest regions,
   interpolation, prediction, reconciliation, late join, and reconnect/resume.
4. Add lag/loss integration tests and a permissioned relay fallback only after
   direct and LAN paths are covered.

Exit gate: an untrusted client cannot author state or exhaust unbounded server
   work; packet loss/reordering/latency/reconnect/late join are tested; secure
   sessions negotiate compatible protocol versions.

### Track C — Conventional 2D game runtime

1. Add backend-neutral audio commands/resources and a bounded runtime mixer
   adapter.
2. Add retained UI layout/input primitives, fonts, focus/navigation, and
   accessibility-safe defaults.
3. Finish SDL gamepad/touch event translation, culling/layers/parallax, and
   screen effects on top of the backend-neutral APIs.
4. Complete physics/content contracts needed by the templates without making
   cellular terrain mandatory.

Exit gate: a side-scroller, top-down/RTS, RPG, and visual-novel sample use
   public APIs for input, rendering, audio, UI, save/load, and packaging.

### Track D — Authoring and extension

1. Finish scene tree, viewport, inspector, tile/terrain painting, collision and
   animation editing, undo/redo, autosave, play-in-editor, and hot reload.
2. Stabilize a versioned C ABI over the supported runtime subset.
3. Add a sandboxed Lua/mod boundary with command-only mutations, budgets,
   serialization, and replay-safe hooks.
4. Keep MCP loopback/HTTP transports permissioned, audited, rate-limited, and
   unable to bypass gameplay authority.

Exit gate: a new contributor can create and edit a project without manually
rewriting generated engine code, and an extension cannot access raw filesystem,
network, or authoritative state without an explicit capability.

### Track E — Ecosystem and compatibility

1. Build production samples for every template family using the same save,
   editor, and package paths.
2. Publish beginner documentation, threat model, security policy, API/ABI
   compatibility policy, and contribution guidance.
3. Add macOS packaging, package-manager recipes, signed artifacts, crash
   reporting guidance, and release cadence.
4. Maintain cross-version save/replay fixtures and a compatibility matrix.

Exit gate: a first-time contributor can build, test, document, package, and
upgrade a sample on every supported platform.

## Immediate implementation queue

1. Add a generation manifest and recovery tests around `ChunkStore`. (In
   progress: atomic generation commit is implemented; entity/session state is
   still outside the store.)
2. Define and implement the first versioned session-state envelope by
   composing the world snapshot, scene serialization, and subsystem metadata
   codecs without claiming that every subsystem is persisted before its codec
   exists.
3. Add authoritative command recording hooks to replay.
4. Add security/replication acceptance tests before introducing encryption or
   relay complexity.
5. Build audio/UI/platform adapters and one production-quality sample per
   broad template family.

Every completed item must include public API documentation, focused tests, a
CI path where practical, and a clear limitation note when the implementation
is intentionally partial.
