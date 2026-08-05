# Migration notes

This document covers changes that can affect applications consuming Meat2D.
It is intentionally kept next to the source tree so a release can be reviewed
without relying on commit history.

## 0.3.x to 0.4.x

- Use the exported CMake targets `Meat2D::Core`, `Meat2D::Render`,
  `Meat2D::Audio`, `Meat2D::Net`, and `Meat2D::Tools`. Do not link implementation libraries or
  include files from `src/`.
- The engine is a general 2D runtime. Cellular/destructible simulation is an
  optional gameplay system; it is not required by conventional side-scrollers,
  top-down games, RTS games, RPGs, visual novels, or metroidvanias.
- Project creation is template-driven. Treat a template identifier as a
  project starting point rather than as a runtime mode; projects can compose
  systems after creation.
- Scene files and replay files are versioned. Applications should use the
  public serialization/replay APIs and surface migration failures instead of
  silently accepting a partial load.
- Fixed-size world persistence is available through `ChunkStore`, but it does
  not yet page an unbounded world or restore living-agent/organism state. Keep
  those limitations visible in save-game UX.
- Network sessions remain server-authoritative. Deterministic replay is a
  validation tool, not permission for clients to author authoritative state.

## Build and packaging

- CMake 3.24 or newer and a C++20 compiler are required.
- Headless consumers should disable the SDL/client, launcher, and example
  options. The `headless` preset demonstrates the supported configuration.
- `MEAT2D_BUILD_FUZZERS=ON` requires Clang with libFuzzer and is intentionally
  off by default.
- Consumers should pin a release tag (for example `v0.4.0`) instead of
  tracking `main` in production.
