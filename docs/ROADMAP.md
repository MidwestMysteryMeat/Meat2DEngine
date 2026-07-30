# Roadmap

## 0.1 — deterministic sand core

- [x] Public MIT repository
- [x] Portable C++20/CMake structure
- [x] Chunked eight-byte cells
- [x] Active/sleeping chunks
- [x] Sand, water, stone, and empty space
- [x] Deterministic state hashing
- [x] Interactive SDL3 lab and headless target
- [x] Tests and baseline benchmark
- [x] Initial install/CPack support
- [x] Expanded element and reaction set
- [ ] Dirty-region texture uploads

## 0.2 — living laboratory

- [x] Temperature transfer and phase changes
- [x] Fire, smoke, steam, oil, wood, metal, acid, and plant life
- [ ] Utility-driven embodied agents
- [ ] Cellular organisms with reproduction and mutation
- [ ] Deterministic entity command buffer
- [ ] Agent perception and grid pathing

## 0.3 — authoritative multiplayer

- [ ] UDP transport
- [ ] Connection and handshake state
- [ ] Reliable-message channel
- [ ] Input commands and server tick loop
- [ ] Chunk interest management
- [ ] Snapshot and chunk-delta serialization
- [ ] Prediction, reconciliation, and state-hash diagnostics
- [ ] Latency/loss simulation tests

## 0.4 — game creation and packaging

- [ ] Public client/rendering library target
- [ ] `meat2d new` starter-game scaffolder
- [ ] Side-view and top-down starter templates
- [ ] Asset and settings manifests
- [ ] Windows/Linux client and headless-server bundles
- [ ] Reusable GitHub Actions build/release workflow
- [ ] Tagged SDK archives and example game

## Later

- Parallel deterministic chunk scheduling
- Persistent/infinite world streaming
- Shooter-oriented collision and projectiles
- Destructible terrain queries
- Scripting/mod extension boundary
- Profiling overlay and replay inspector
