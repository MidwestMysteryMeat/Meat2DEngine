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

The environment side should follow the same separation used by game-agent
toolkits: a bounded observation/action adapter inside the engine, an offline
trainer, and a validated inference artifact at runtime. Unity's
[ML-Agents Toolkit](https://github.com/Unity-Technologies/ml-agents) demonstrates
multi-agent, imitation-learning, curriculum, randomization, and concurrent
environment workflows; [UnrealMLAgents](https://github.com/AlanLaboratory/UnrealMLAgents)
shows how that model can be adapted to an engine plugin. Meat2D's planned
equivalent is a headless environment runner that emits observations and accepts
validated commands, not a Python or GPU dependency in the authoritative tick.
The smaller [CPP_Neural_Network](https://github.com/Krish120003/CPP_Neural_Network)
project is a useful reference for supervised datasets, evaluation splits, and
activation/loss experiments, but its floating-point training loop should remain
offline rather than being copied into deterministic multiplayer simulation.

Lua is a good optional authoring/training surface for projects that already use
Lua gameplay scripts. [`luann`](https://github.com/wixico/luann) demonstrates a
small train/save/load workflow, while [`LuaNet`](https://github.com/Maia-jp/LuaNet)
demonstrates a beginner-friendly multilayer API and matrix helpers. Neither is
treated as an authoritative runtime dependency: a future Lua integration must
use the same sandbox, instruction budget, deterministic RNG, command-only
mutation boundary, and validated model-export path as C++ agents.
