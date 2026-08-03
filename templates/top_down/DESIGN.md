# Top-down / RTS starter

This template starts with:

- a deterministic 320×180 arena containing solids and simulated fluids;
- a simple four-direction integer controller;
- fire painting as a placeholder weapon/environment interaction;
- nearest-neighbor SDL3 material rendering;
- `Meat2D::Net` linked for authoritative input, chunk replication, and optional
  multiplayer sessions;
- a shared top-down foundation for action games, tactics, and RTS projects.

Controls: `WASD` moves, left mouse emits fire, and `Esc` quits. Launch with
`--connect HOST [PORT]` to exercise the authoritative multiplayer client seam.
The same starter can grow from one controllable actor into selectable units,
validated orders, economy, fog-of-war, and deterministic RTS simulation.

Replace the starter player with your own entity components, collision shape,
weapons, and camera. Server-authoritative movement can use the same
target-tick input pattern demonstrated by the engine's material paint command.
