# Meat2D architecture

## Design constraints

Meat2D is designed around four constraints:

1. Cellular worlds can contain millions of individually addressable positions.
2. The same state must run in an interactive client and a headless server.
3. Multiplayer replication must be bounded by player interest, not total world
   size.
4. A new game should consume the engine without copying its source tree.

The renderer, AI, networking, and game rules are consumers of the simulation
core. They do not own authoritative cells.

## State flow

```text
player/network commands ─┐
AI commands ─────────────┼──► fixed simulation tick
material reactions ──────┘             │
                                       ▼
                              chunked world state
                               │       │       │
                               ▼       ▼       ▼
                           renderer  snapshots  state hash
```

Commands will be ordered by tick, player, and sequence before application.
That ordering is part of the determinism contract.

## Cellular world

The world is divided into 64×64 chunks. A chunk currently stores 4,096
eight-byte cells, or 32 KiB of cell state before metadata.

Each cell contains:

- Stable material ID
- Visual variant
- One-byte update epoch
- One-byte material state used for lifetime, potency, growth, or electric charge
- Fixed-point temperature in sixteenths of a degree Celsius
- Small signed X/Y velocity channels

Authoritative cells contain no floating-point values.

### Activity and sleeping

Mutating a cell wakes its chunk and neighboring chunks. A chunk that produces
no changes for a configured number of ticks sleeps. Sleeping chunks are skipped
by cellular evaluation until an edit, entity, reaction, or neighboring movement
wakes them.

Chunks also track local dirty bounds and a monotonically increasing revision.
The renderer can upload dirty areas, while networking can compare acknowledged
revisions and send only relevant chunk deltas.

### Update ordering

Gravity materials are traversed bottom-to-top. Horizontal traversal is selected
from a deterministic hash of world seed, coordinate, and tick, preventing a
permanent directional bias without consuming mutable random-generator state.

An update epoch prevents a moved cell from being processed twice in one tick.
Epoch bytes are reset every 255 ticks to make wraparound explicit.

### Materials and reactions

Material IDs are explicit serialized values and are append-only. Definitions
provide phase, density, dispersion, thermal conductivity, default and ignition
temperatures, blast resistance, and behavior flags.

Each active non-empty cell samples a deterministic cardinal heat exchange,
evaluates phase and ignition thresholds, runs its material-specific reaction,
then attempts phase-appropriate movement. Granular and liquid cells move with
gravity, gases rise, and density controls displacement. Reactions use only
integer and fixed-point state. See [MATERIALS.md](MATERIALS.md) for the current
catalog and interaction rules.

## Determinism

Determinism is tested by executing equal worlds side by side and comparing a
64-bit state hash after every tick. The hash covers dimensions, seed, tick, and
gameplay-relevant cell fields. Ephemeral update stamps and chunk scheduling
metadata are excluded.

Future multithreading will operate on dependency-safe chunk phases and merge
commands in a stable order. Parallel execution cannot be allowed to select a
different winner for contested cells.

## Multiplayer

The networking baseline supports two to eight players:

- Dedicated authoritative server and reusable client/server session classes
- Fixed server ticks
- Input sequence numbers and acknowledgement bitfields
- Per-client chunk interest
- Reliable control messages over an unreliable packet transport
- Periodic snapshots with chunk revision deltas
- State-hash diagnostics and a replicated client material world
- LAN discovery and direct local/IP joins
- Expiring public listings and directory-assisted UDP hole punching

Wire values are explicitly little-endian encoded; native struct memory is never
sent. Packets stay at or below 1,200 bytes. Large RLE chunk messages are split
into reliable fragments and reassembled out of order. Each connected client
tracks a focus-centered chunk interest region and known chunk revisions.

Hello/Welcome exchanges bind a client nonce to a server-generated session
token. Subsequent inputs must carry that token, increase their input sequence,
fit a narrow future-tick window, pass a per-update rate budget, and satisfy
world/material limits before entering the simulation command buffer.

Movement prediction, reconciliation, encryption, identity authentication, and
a relay fallback remain later layers. Direct, LAN, and public-directory hosting
are described in [DISCOVERY_AND_HOSTING.md](DISCOVERY_AND_HOSTING.md). See
[NETWORKING.md](NETWORKING.md) for the gameplay wire protocol.

## AI direction

Two AI scales share the same world:

- Embodied utility agents perceive nearby material, danger, resources, and
  reachable cells, then emit ordinary world/entity commands.
- Cellular organisms use compact state and local rules for consumption,
  reproduction, mutation, and environmental response.

Embodied agents never receive mutable world storage. Their planned actions
enter the same command buffer used by external controllers. Commands are
ordered by target tick, issuer, sequence, type, and target before validation
and application. An external command for an agent suppresses autonomous
planning for that tick, which creates a clean handoff for players or servers.

Cellular organisms use a synchronous double-buffered field. Each occupied
position carries an eight-byte genome/energy/age record. Alternating traversal
and coordinate/tick noise reduce directional bias without compromising replay.
See [AI_AND_LIFE.md](AI_AND_LIFE.md).

## Rendering

The renderer writes the world to an RGBA streaming texture and lets SDL3
scale it with nearest-neighbor sampling. This is intentionally simple and keeps
simulation work measurable.

Uploads are dirty-region driven. Chunks accumulate local dirty bounds until a
renderer consumes them: each frame the living lab rasterizes and uploads only
`chunk_dirty_rect` regions (plus chunks covered by organism/agent overlays on
the previous or current frame), then calls `clear_dirty` on the displayed
world. The same path serves the local simulation and the replicated
multiplayer world, whose cells are marked dirty as chunk deltas apply. A full
upload happens only on the first frame, world switches, resets, and texture
recreation. Later rendering can add sprite/entity passes without changing the
cell-state API.

## Packaging

The `meat2d_core`, `meat2d_net`, and `meat2d_tools` libraries export as
`Meat2D::Core`, `Meat2D::Net`, and `Meat2D::Tools`. They support:

- Direct `add_subdirectory`
- CMake `FetchContent`
- Installation plus `find_package(Meat2D CONFIG REQUIRED)`
- CPack ZIP/TGZ SDK archives

The CLI and graphical editor build on `Meat2D::Tools` for starter generation,
project-root-confined code/asset access, background build/test/package tasks,
and explicit GitHub publishing. Sprite metadata remains plain TOML and its
validated runtime parser lives in `Meat2D::Core`.
