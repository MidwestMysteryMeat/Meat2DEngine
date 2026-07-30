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
- Flags
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

## Determinism

Determinism is tested by executing equal worlds side by side and comparing a
64-bit state hash after every tick. The hash covers dimensions, seed, tick, and
gameplay-relevant cell fields. Ephemeral update stamps and chunk scheduling
metadata are excluded.

Future multithreading will operate on dependency-safe chunk phases and merge
commands in a stable order. Parallel execution cannot be allowed to select a
different winner for contested cells.

## Multiplayer direction

The first networking target is two to eight players:

- Dedicated or listen authoritative server
- Fixed server ticks
- Input sequence numbers and acknowledgement bitfields
- Per-client chunk interest
- Reliable control messages over an unreliable packet transport
- Periodic snapshots with chunk revision deltas
- Client-side movement prediction and reconciliation
- State-hash diagnostics and recorded command streams

The protocol header is versioned from the first commit. Networking transport
and serialization are not implemented yet; the header reserves their stable
contract.

## AI direction

Two AI scales will share the same world:

- Embodied utility agents perceive nearby material, danger, resources, and
  reachable cells, then emit ordinary world/entity commands.
- Cellular organisms use compact state and local rules for consumption,
  reproduction, mutation, and environmental response.

AI will not receive mutable access to world storage. This preserves command
ordering, replay, and server authority.

## Rendering

The first renderer writes the world to an RGBA streaming texture and lets SDL3
scale it with nearest-neighbor sampling. This is intentionally simple and keeps
simulation work measurable.

Later rendering can upload only dirty chunk regions and add sprite/entity
passes without changing the cell-state API.

## Packaging

The `meat2d_core` library exports as `Meat2D::Core`. It supports:

- Direct `add_subdirectory`
- CMake `FetchContent`
- Installation plus `find_package(Meat2D CONFIG REQUIRED)`
- CPack ZIP/TGZ SDK archives

Planned tooling adds a starter-game generator, asset conventions, client/server
release bundles, and reusable GitHub publishing workflows.
