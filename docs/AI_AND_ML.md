# AI, machine learning, and MCP boundary

Meat2D supports three related but separate AI surfaces:

1. **Deterministic game AI and crowds.** `CrowdSimulation` provides stable-ID
   integer steering, local separation, and bounds. Existing living agents use
   the authoritative command queue. Both can be used by top-down/RTS,
   side-scroller, RPG, sandbox, and cellular templates.
2. **Machine-learning agents.** `FixedNeuralNetwork` runs exported inference
   models using fixed-point arithmetic. `MachineLearningAgent` selects actions,
   accumulates rewards, and enforces decision budgets. Training, data capture,
   and model conversion belong in external tools and are not part of a
   production server tick.
3. **MCP tooling.** A future optional Model Context Protocol bridge can let
   approved AI assistants inspect projects, scenes, assets, tests, and build
   results. Mutations must go through the same editor/scene APIs, permissions,
   validation, undo history, and audit logging as a human editor.

## Safety and determinism rules

- Neural inference must be bounded by layer, unit, parameter, and decision
  budgets.
- Training must not execute inside an authoritative multiplayer tick.
- ML and scripted agents emit validated commands; they do not mutate `World`,
  `Scene`, or network state directly.
- MCP is never a gameplay transport and cannot bypass server authority.
- Filesystem, process, network, publish, and destructive editor operations are
  separate permissions and should default to disabled.
- Every AI-assisted mutation should be replayable through scene history or the
  authoritative command log.
