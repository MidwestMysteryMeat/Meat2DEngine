# 2D template taxonomy and production gate

Meat2D is a general-purpose 2D engine. The cellular world is an optional
subsystem, not the definition of a Meat2D game. Templates are independent
starting points that select useful defaults, sample code, scene conventions,
and networking expectations for a genre.

## Starter families

| Starter | Covers | First systems it should demonstrate |
| --- | --- | --- |
| Side scroller | platformers, run-and-gun, arcade action | fixed tick, kinematic movement, camera, animation |
| Action platformer | Castlevania-style room exploration and combat | room transitions, gates, hitboxes, checkpoints |
| Top-down | action adventure, twin-stick, dungeon games | 8-way movement, camera, interaction, projectiles |
| Top-down RTS | multiplayer strategy and tactics | authoritative commands, units, economy, fog-of-war seam |
| RPG | turn-based or real-time party games | inventory, dialogue, quests, encounters, save data |
| Visual novel | dialogue-first and branching narrative games | story state, choices, localization, portraits, save slots |
| Destructible artillery | Liero/Worms-like games | deterministic terrain edits, weapons, rounds, replay |
| Cellular roguelite | Noita-like material-driven action | cellular reactions, procedural rooms, bounded effects |
| Falling sand | focused cellular experiments and toy worlds | material catalog, painting, reactions, inspection |
| Sandbox survival | Terraria-like building, exploration, crafting | camera/streaming, tile content, inventory, progression |

These are capability families, not promises to reproduce a particular
commercial game. A project can start from one family and combine systems from
another without forking the engine.

## Production/parity gate

Meat2D is production/parity ready when every starter can use the same public
engine surfaces for the following:

1. **Runtime:** stable entity/component IDs, parenting, layers/tags/groups,
   scenes/prefabs, fixed simulation ticks, interpolation, events, transitions,
   deterministic RNG, collision/physics, and safe multithreaded scheduling.
2. **Rendering/content:** texture and atlas importing, sprite batching,
   animation, tilemaps, collision metadata, camera culling/parallax, particles,
   shaders/effects, fonts, color management, and backend adapters.
3. **Game services:** action maps and rebinding, gamepad/touch input, audio and
   music, UI/layout, localization, save slots, settings, achievements, and a
   debug console/profiler.
4. **Authoring:** scene tree, viewport, inspector, tile/terrain painting,
   animation/collision editors, prefabs and overrides, undo/redo, autosave,
   diffs, play-in-editor, hot reload, and asset dependency diagnostics.
5. **Extension:** versioned C ABI, documented C++ API, sandboxed Lua or another
   selected scripting runtime, deterministic command-only mutations, budgets,
   serialization, and replay-safe hooks.
6. **Multiplayer:** general entity/component replication, server authority,
   prediction/interpolation, late join, reconnection, protocol negotiation,
   authentication/encryption, rate limits, observability, and optional relay.
7. **Persistence/release:** complete authoritative saves and replays, schema
   migrations, streamed-world addressing, crash-safe writes, package consumers,
   supported desktop builds, signed/checksummed archives, SBOMs, crash reports,
   compatibility policy, and reproducible CI.

The current repository has a strong deterministic simulation/networking base and
an early scene/editor foundation. The remaining work is primarily the general
runtime/content/editor surface and the production hardening around it.
