# Top-down starter

This template starts with:

- a deterministic 320×180 arena containing solids and simulated fluids;
- a simple four-direction integer controller;
- fire painting as a placeholder weapon/environment interaction;
- nearest-neighbor SDL3 material rendering;
- `Meat2D::Net` linked for authoritative input and chunk replication.

Controls: `WASD` moves, left mouse emits fire, and `Esc` quits.

Replace the starter player with your own entity components, collision shape,
weapons, and camera. Server-authoritative movement can use the same
target-tick input pattern demonstrated by the engine's material paint command.
