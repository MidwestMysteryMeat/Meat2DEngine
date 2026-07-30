# Roadmap

## 0.1 — deterministic sand core

- [x] Public Apache-2.0 repository with NOTICE
- [x] Portable C++20/CMake structure
- [x] Chunked eight-byte cells
- [x] Active/sleeping chunks
- [x] Sand, water, stone, and empty space
- [x] Deterministic state hashing
- [x] Interactive SDL3 lab and headless target
- [x] Tests and baseline benchmark
- [x] Initial install/CPack support
- [x] Expanded element and reaction set
- [x] Dirty-region texture uploads

## 0.2 — living laboratory

- [x] Temperature transfer and phase changes
- [x] Fire, smoke, steam, oil, wood, metal, acid, and plant life
- [x] Utility-driven embodied agents
- [x] Cellular organisms with reproduction and mutation
- [x] Deterministic entity command buffer
- [x] Agent perception and local terrain movement

## 0.3 — authoritative multiplayer

- [x] UDP transport
- [x] Connection and session-token handshake state
- [x] Reliable-message channel
- [x] Input commands and server tick loop
- [x] Chunk interest management
- [x] Snapshot and chunk-delta serialization
- [x] Direct joins, LAN discovery, and public server listings
- [x] Self-hostable directory and UDP hole-punch introductions
- [x] Prediction, reconciliation, and state-hash diagnostics
- [x] Loss, retransmission, duplicate, wraparound, and loopback tests

## 0.4 — game creation and packaging

- [x] Public client/rendering library target
- [x] `meat2d new` starter-game scaffolder
- [x] Side-view and top-down starter templates
- [x] Asset and settings manifests
- [x] Graphical project editor with a root-confined code/asset browser
- [x] PNG/JPEG preview and sprite-sheet animation manager
- [x] External code/asset refresh with conflict-safe editor buffers
- [x] One-click background build, game test, package, and GitHub publish
- [x] Editor-hosted local sessions and direct/LAN/public one-click joins
- [x] Windows/Linux client and headless-server bundle definitions
- [x] Reusable GitHub Actions build/release workflow
- [x] Tagged SDK archives and example game

## Later

- Parallel deterministic chunk scheduling
- Persistent/infinite world streaming
- Shooter-oriented collision and projectiles
- [x] Destructible terrain queries — deterministic `World::raycast`/`line_of_sight`
- Scripting/mod extension boundary
- Profiling overlay and replay inspector
- Authenticated relay fallback for restrictive NAT
