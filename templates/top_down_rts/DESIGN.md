# Top-down RTS starter

This starter is for multiplayer-first real-time strategy: authoritative
command input, selectable units, resource/economy state, fog-of-war, build
queues, and deterministic simulation. It includes a network client seam and
uses the engine's authoritative UDP server/session layer rather than treating
multiplayer as an afterthought.

Run a dedicated server from the Meat2D SDK, then launch the generated game
with `--connect HOST [PORT]`. The starter begins with focus/selection commands;
replace those with validated unit orders and replicate the complete RTS state
through a game-specific command protocol.
