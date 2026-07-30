# Material catalog

Meat2D 0.1 currently ships 25 stable material IDs. IDs are serialized and
replicated, so existing values are never reordered; new materials are appended
before the `Count` sentinel.

| ID | Material | Phase | Primary behavior |
| ---: | --- | --- | --- |
| 0 | Empty | Empty | Open space |
| 1 | Sand | Granular | Falls and sinks through lighter fluids |
| 2 | Water | Liquid | Flows, freezes at 0 °C, boils at 100 °C |
| 3 | Stone | Solid | Corrodible, blast-resistant terrain |
| 4 | Wood | Solid | Organic, flammable structure |
| 5 | Oil | Liquid | Light, flowing fuel |
| 6 | Fire | Gas | Rises, heats and ignites neighbors, expires into smoke |
| 7 | Smoke | Gas | Rises and dissipates |
| 8 | Steam | Gas | Rises and condenses below 90 °C |
| 9 | Metal | Solid | Highly conductive, carries electric charge |
| 10 | Acid | Liquid | Corrodes eligible neighboring material until neutralized |
| 11 | Plant | Solid | Flammable life that grows near water |
| 12 | Seed | Granular | Germinates on wet soil, mud, or plants |
| 13 | Soil | Granular | Becomes mud beside water |
| 14 | Mud | Liquid | Slow-flowing wet soil; dries under high heat |
| 15 | Salt | Granular | Falls and dissolves beside water |
| 16 | Snow | Granular | Melts above 0 °C |
| 17 | Ice | Solid | Melts above 2 °C |
| 18 | Lava | Liquid | Hot, slow, ignites fuel and cools against water |
| 19 | Obsidian | Solid | Strong cooled lava |
| 20 | Gunpowder | Granular | Explodes above its ignition temperature |
| 21 | Explosive gas | Gas | Spreads and produces a larger blast when ignited |
| 22 | Concrete | Solid | Strong but corrodible construction |
| 23 | Electricity | Energy | One-tick pulse that charges conductors and ignites fuel |
| 24 | Debris | Granular | Loose residue from damaged terrain |

## Deterministic interaction order

For each active cell, the authoritative simulation:

1. Exchanges fixed-point heat with one coordinate/tick-selected neighbor.
2. Applies phase changes and temperature-based ignition.
3. Runs material logic such as corrosion, growth, charge propagation, or blast.
4. Attempts gravity, buoyancy, and dispersion movement.

Coordinate- and tick-derived noise chooses directions and reaction opportunities
without mutable random-generator state. Equal worlds receiving equal commands
therefore retain equal hashes after every tick.

Temperatures are signed 1/16 °C fixed point. No floating-point value enters the
authoritative cell state or reaction calculations.
