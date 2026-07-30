# Contributing

Meat2D is pre-alpha. Small changes with tests and a clear simulation or engine
benefit are easiest to review.

## Local checks

```bash
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
./build/headless/meat2d_benchmark
```

Run the `dev` preset when changing the SDL client.

## Core rules

- Authoritative state uses deterministic integer or fixed-point values.
- Existing serialized material and packet IDs are never reordered.
- Simulation code must run without a renderer.
- Hot loops should not allocate.
- World mutation should pass through stable commands or `World` APIs.
- New behavior needs a test that can fail deterministically.
- Benchmark changes that materially reduce throughput should explain why.

Format C++ with the repository `.clang-format` configuration. Keep public API
under `include/meat2d` and implementation under `src`.
