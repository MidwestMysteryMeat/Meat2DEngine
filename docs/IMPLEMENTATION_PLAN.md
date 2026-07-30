# Implementation plan — remaining "Later" roadmap items

This is a working plan for the three items still open in [ROADMAP.md](ROADMAP.md)'s
"Later" section, written against the actual current code (file/function references
below are real, not aspirational). Each item gets its own phased approach, explicit
risk callouts, and — where a plan hinges on a decision only the project owner should
make — an open question instead of an assumption. Update this file as work lands or
the plan changes; it isn't meant to be a one-time snapshot.

## 1. Unbounded / streamed world addressing

**Goal.** Let a world's playable area grow beyond the size it was constructed with —
chunks page in as players explore, instead of `World` owning one fixed-size array for
its whole lifetime.

**Current constraint.** `World` stores chunks in a flat `std::vector<Chunk> chunks_`
sized `chunk_columns_ * chunk_rows_` at construction (`src/sim/World.cpp`), addressed
by `chunk_index(Vec2i) = (y/64)*chunk_columns_ + (x/64)`. Six call sites assume this
dense, `0..N-1`, fixed-size layout via `World::chunk_columns()`/`chunk_rows()`/
`chunks()`/`chunk_dirty_rect()`:

| File | Uses | What it does with chunk indices |
| --- | --- | --- |
| `src/net/ChunkCodec.cpp` | 8 | Encodes/decodes chunk deltas by `(column, row)` for network sync |
| `src/render/WorldView.cpp` | 3 | Iterates the full grid each frame to find dirty regions |
| `src/sim/ChunkStore.cpp` | 4 | Iterates the full grid to save/load (this session's work) |
| `src/net/Session.cpp` | 3 | Chunk-interest management (which chunks a client is sent) |
| `tests/simulation_tests.cpp` | several | Assume `chunk_columns()/chunk_rows()` are stable per-`World` |

**Phased approach.**

1. **Storage swap.** Replace `std::vector<Chunk> chunks_` with a sparse map keyed by
   `(int32_t column, int32_t row)` — `std::unordered_map` with a packed 64-bit key
   (`(uint64_t(column) << 32) | uint32_t(row)`, avoiding a custom hash for a pair).
   `World::in_bounds` stops meaning "inside a fixed rectangle" and starts meaning
   "chunk exists or can be created" — needs a real design decision (see open question
   below) about whether the world has *any* outer bound at all, or is truly infinite
   modulo `int32_t` chunk-coordinate range.
2. **On-demand chunk creation.** Any cell read/write for an unloaded chunk creates it
   (default-empty, or via a pluggable generation callback — out of scope for phase 1,
   but the seam should exist so `Scenario.hpp`-style procedural generation can hook in
   later). Decide and document eviction policy (LRU to `ChunkStore`? unbounded resident
   set until a memory budget is set explicitly?).
3. **Update six call sites.** Each needs an "iterate chunks within a *region of
   interest*" pattern instead of "iterate `0..chunk_columns()*chunk_rows()`" —
   `WorldView` already has a natural region (the camera/render viewport);
   `net::Session`'s chunk-interest system already has a natural region (each client's
   interest radius, `interest_radius_chunks` in `ServerConfig`); `ChunkCodec`'s wire
   format currently encodes chunk index as `row*columns+column` (implicitly assumes a
   known grid width) — needs to switch to encoding explicit `(column, row)` pairs,
   which changes the network protocol version.
4. **Re-verify `step_parallel`.** Its 4-phase `(column%2, row%2)` grouping still holds
   for unbounded coordinates (parity doesn't care about sign or magnitude), but the
   "gather active chunks per phase" step currently does two nested `for` loops over
   `0..chunk_rows_`/`0..chunk_columns_` — needs to iterate the sparse map's *currently
   loaded and active* chunks instead.
5. **Re-verify `ChunkStore`.** Already file-per-chunk by `(column, row)` — this part
   barely changes; `save_all`/`load_all`'s "iterate the world's grid" loops become
   "iterate loaded chunks" (a strict shrink of scope, not a redesign).

**Risks.** This is the largest of the three items — it touches the hottest, most
heavily-tested part of the engine (every physics test, the network protocol, the
renderer). A protocol version bump means old and new clients/servers can't interoperate
— needs a compatibility decision (bump `protocol_version` in `Protocol.hpp` and accept
a breaking change, since this is pre-1.0 and no back-compat guarantee has been made).

**Open questions for the owner.**
- Does "unbounded" mean truly infinite (any `int32_t` chunk coordinate), or a large but
  still-fixed maximum (e.g. "up to 10,000×10,000 chunks", which simplifies some of the
  above without the full sparse-map rewrite — a `std::vector` with a much larger
  reserved size might suffice if the real ask is "big worlds", not "infinite" ones)?
- Is on-demand procedural generation for newly-created chunks in scope now, or is
  "new chunks start empty" acceptable for a first pass?

## 2. Scripting / mod extension boundary

**Goal.** Let games (or mods of games) built on Meat2D hook simulation events —
material reactions, agent AI, tick lifecycle — without recompiling engine or game C++.

**Current state.** No embedded scripting exists anywhere in the codebase today. The
closest analog is `templates/`'s scaffolded C++ starter projects (compile-time
extension only) and `apps/launcher`'s project editor (edits/rebuilds C++ source, not a
runtime script layer).

**This is a technology decision, not just an implementation task — do not start
building against an assumed language without confirming it first.** The user's other
projects (Frosthold, MMOLite) use Lua/LuaJIT; that's a reasonable default to propose,
but "reasonable default" isn't the same as "confirmed."

**Phased approach (once a language is chosen — sketched for Lua as the leading
candidate, adjust if a different choice is made).**

1. **Embed the interpreter.** `FetchContent`-vendor LuaJIT or Lua 5.4 (matching the
   pattern already used for SDL3/imgui in `CMakeLists.txt`), behind a new
   `MEAT2D_BUILD_SCRIPTING` option (default `ON`, consistent with the other feature
   flags) so headless/server-only builds can opt out.
2. **Define the hook surface.** A small, explicit set of C++ call sites that invoke
   into script if a handler is registered — start narrow and grow, not the reverse:
   - `on_material_transform(cell_position, from_material, to_material)` — natural spot
     in `World::transform_cell` (`src/sim/World.cpp`).
   - `on_tick_start(tick)` / `on_tick_end(tick, TickStats)` — natural spot in
     `World::step`/`step_parallel`'s entry/exit.
   - `on_agent_command(EntityCommand)` — natural spot in
     `LivingSimulation::apply_command` (`src/ai/LivingSimulation.cpp`), letting mods
     override or veto AI decisions.
3. **Sandbox boundary.** Scripts get a read/query API (`material_at`, `raycast`,
   `line_of_sight` — the last two already exist as clean, side-effect-free `World`
   methods, ideal first exposure candidates) and a narrow, explicit mutation API
   (`set_material`, `spawn_agent`), not raw access to `World` internals. No filesystem
   or network access from script by default.
4. **Determinism boundary — the hard part.** The engine's whole design center is
   deterministic replay (`docs/ARCHITECTURE.md`'s Determinism section,
   `meat2d::replay`, multiplayer's `state_hash` equality contract). A script hook that
   reads wall-clock time, uses its own RNG, or iterates a hash-table in
   nondeterministic order breaks that contract silently. This needs either (a) a
   documented, enforced-by-convention rule ("script hooks must be pure functions of
   the arguments they're given, no scripting-side global mutable state"), or (b) actual
   enforcement (a restricted Lua environment with `os`/`io` stripped, and a
   deterministic RNG handle passed into hooks instead of scripts calling `math.random`
   directly). Recommend (b) — a convention nobody enforces will get violated.
5. **Test the boundary itself.** Before any real hook ships, a
   `test_scripted_determinism` test mirroring `test_determinism`
   (`tests/simulation_tests.cpp`): two identical worlds, one with a script hook
   attached that does something (paints a material, say), stepped in lockstep,
   asserting equal `state_hash()` every tick — proving the scripting layer doesn't
   quietly break the contract everything else in this engine is built on.

**Risks.** Determinism regression is the real danger here, not the embedding itself
(FetchContent-vendoring an interpreter is mechanical). Scope creep is the other real
danger — "mod extension boundary" can balloon into an entire modding SDK; start with
the three hooks above and stop there for a first landing.

**Open question for the owner.** Confirm Lua (or state a different preference) before
any implementation starts.

## 3. Authenticated relay fallback for restrictive NAT

**Goal.** When direct P2P connection and UDP hole-punch both fail (symmetric NAT,
restrictive firewalls), route gameplay packets through a relay hop instead of failing
the connection outright.

**Current state.** `include/meat2d/net/Discovery.hpp` already has the introduction
pieces this builds on: `DirectoryPunchMessage`/`HolePunchMessage` (peer-address
exchange for direct hole-punch attempts) and `PublicDirectoryServer` (a working,
tested, ~770-line self-hostable directory in `src/net/Discovery.cpp`). There is no
relay/forwarding code anywhere yet — this is new surface, not an extension of
something half-built.

**Flagged as security-sensitive — treat with the same care as any dual-use networking
primitive.** A relay is a real abuse surface if not done carefully: it could become an
open UDP reflector/amplifier, a way to inject packets as if from a trusted peer, or a
bandwidth-abuse vector if unmetered. This section leans deliberately conservative.

**Phased approach.**

1. **Design the auth boundary first, before any relay code.** Every relayed packet
   must prove it belongs to an already-authorized session — the natural anchor is the
   session-token handshake `AuthoritativeServer`/`ClientSession` already do
   (`include/meat2d/net/Session.hpp`, `docs/ARCHITECTURE.md`'s Multiplayer section
   mentions "session-token handshake state"). A relay should only ever forward packets
   between two endpoints that have *already* completed that handshake directly with
   each other or with the directory — it must never be usable to originate a session,
   only to carry bytes for one that's already been authorized. Write this down as an
   explicit protocol doc addition to `docs/NETWORKING.md` before writing relay code.
2. **Bound the abuse surface explicitly.** Rate-limit per relayed session
   (bytes/second, matching `PublicDirectoryServer`'s existing
   `maximum_datagrams_per_update`/`lease_timeout` pattern in `Discovery.hpp`'s
   `DirectoryConfig`), a hard session count cap, and a lease timeout so an abandoned
   relay slot doesn't linger. A relay should refuse to relay to/from an endpoint that
   isn't part of a session it authorized, full stop — no "just forward whatever shows
   up on this port."
3. **Add the relay hop as a new, separate role** (`meat2d_relay`, mirroring
   `meat2d_directory`'s existing pattern as a standalone app under `apps/`), not folded
   into the existing directory or dedicated-server code — keeps the new attack surface
   isolated to a component nobody has to run unless they opt in, and keeps the
   already-mature `AuthoritativeServer`/`Discovery` code untouched.
4. **Client-side fallback logic.** Attempt direct connect → attempt hole-punch (already
   exists) → only then attempt relay, each with its own timeout, surfaced to the
   caller (`ClientSession` or the sandbox's `--connect` path) as which method actually
   succeeded, both for debugging and so a game can show the player "connected via
   relay (higher latency)" honestly.
5. **Test with induced failure**, not just a happy path: a test harness that
   deliberately blocks direct UDP between two loopback sessions (or simulates
   symmetric-NAT-like address rewriting) and asserts the relay fallback still
   completes a session — the whole point of this feature is the failure path, so the
   happy path passing proves nothing on its own.

**Risks.** This is the item most likely to introduce an actual security hole if rushed
— the auth-boundary design in step 1 needs to be right before anything else is built
on top of it. Recommend treating step 1 as its own reviewable unit before proceeding to
implementation.

**Open question for the owner.** None blocking — the design in step 1 is concrete
enough to start, but flag for review before the relay actually goes live/gets
documented as a supported feature, given the security stakes.

## Suggested order

1. **Scripting boundary** — needs a one-line confirmation (language choice) to unblock,
   then is the most self-contained of the three (new subsystem, doesn't touch existing
   hot paths beyond adding a few optional hook call sites).
2. **Relay fallback** — security-sensitive but well-isolated (new standalone app,
   doesn't touch `World`/`step_parallel`/existing tested networking code).
3. **Unbounded world addressing** — largest, riskiest, touches the most existing
   tested code; do last, and consider splitting further (storage swap → six call sites
   → protocol version bump) into separate reviewable landings rather than one big
   branch, the way parallel chunk scheduling was done this session.
