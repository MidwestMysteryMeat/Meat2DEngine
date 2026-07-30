# Side-scroller starter

This template starts with:

- a 320×180 deterministic material world;
- a simple integer player controller with gravity and stepping;
- a streaming nearest-neighbor SDL3 renderer;
- mouse painting for fast simulation experiments;
- `Meat2D::Net` linked and ready for client/server sessions.

Controls: `A`/`D` move, `W` jumps, left/right mouse paints/erases, and `1`–`4`
select sand, water, stone, or fire.

Replace the starter player with your own entity components and route player
input through `AuthoritativeClient` when enabling network play.
