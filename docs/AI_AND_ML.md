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
3. **MCP tooling.** `McpGateway` is the first transport-neutral Model Context
   Protocol boundary. Future adapters can let approved AI assistants inspect
   projects, scenes, assets, tests, and build results. Mutations must go through
   the same editor/scene APIs, permissions, validation, undo history, and audit
   logging as a human editor.

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

## Training and deployment boundary

The runtime model format is intentionally smaller than a general ML library:
it supports bounded fixed-point inference and deterministic action selection.
An offline trainer/exporter can implement backpropagation, supervised losses,
dataset handling, validation, and model serialization. The
[from-scratch C++ neural-network project](https://github.com/SorawitChok/Neural-Network-from-scratch-in-Cpp)
is a useful educational reference for those algorithms. Projects that need a
larger catalog of optimizers or estimators can use
[mlpack](https://github.com/mlpack/mlpack) in a separate tools package; its
Armadillo/ensmallen/cereal dependencies should not become mandatory for a
minimal game runtime. The planned exporter must record architecture, fixed-
point scale, bounds, and a content hash before a model can be loaded by a
server or replay.
