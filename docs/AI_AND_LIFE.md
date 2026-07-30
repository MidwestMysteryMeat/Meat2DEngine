# AI and life simulation

Meat2D supports two deterministic AI scales. Embodied agents make relatively
expensive decisions and interact through commands. Cellular organisms use
compact local rules and can exist in much larger populations.

## Embodied agents

`meat2d::ai::LivingSimulation` owns a material `World`, an organism field, a
stable-ID agent list, and a future command queue. Its fixed tick performs:

1. Metabolism and environmental damage.
2. Perception and autonomous command planning for uncontrolled agents.
3. Stable sorting and validation of commands for the next tick.
4. Command application.
5. Material simulation.
6. Synchronous cellular-organism simulation.
7. Removal of dead agents.

An external command queued for an agent suppresses its autonomous plan for that
tick. This permits player control, replay, or server input without a separate
entity mutation path.

### Current families

| Family | Needs and perception | Actions |
| --- | --- | --- |
| Grazer | Plants, water, hazardous material | Flee, walk, eat, drink, reproduce |
| Predator | Grazers, hazardous material | Flee, pursue, attack, reproduce |
| Worker | Debris, carried inventory, buildable space, hazards | Flee, collect, haul, place |

Movement includes deterministic gravity, horizontal travel, one-cell steps,
collision checks against material phases, and entity occupancy checks. This is
the baseline local terrain movement model; larger navigation graphs and jump
arcs can build on the same command API.

## Cellular organisms

Every cellular organism is eight bytes:

| Field | Size | Meaning |
| --- | ---: | --- |
| Genome | 32 bits | Eight packed four-bit traits |
| Energy | 16 bits | Metabolism and reproduction budget |
| Age | 16 bits | Lifetime counter |

The genome contains photosynthesis, digestion, motility, reproduction,
heat-preference, resilience, mutation, and pigment traits. Three seed genomes
ship with the engine:

- Photosynthetic colonies favor light exposure and nearby water.
- Decomposers favor plant matter and movement.
- Extremophiles trade growth speed for heat and chemical resilience.

At each tick the field evaluates temperature fitness, hazardous material,
digestion, photosynthesis, movement, reproduction, and age. Reproduction splits
the parent's energy and can flip one genome bit according to the mutation
trait. Organism updates use a second buffer, so a child is never evaluated on
its birth tick.

## Determinism and hashes

Agent and organism logic uses integer state only. Direction choices, aging, and
mutation use hashes derived from seed, coordinate or entity ID, genome, and
tick. `LivingSimulation::state_hash()` combines the material world, organism
field, agents, and queued future commands. Tests execute equal living worlds in
parallel and compare this combined hash after every tick.
