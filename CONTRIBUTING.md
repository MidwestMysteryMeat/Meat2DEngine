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

For memory diagnostics, use the Clang or GCC AddressSanitizer preset:

```bash
cmake --preset asan-headless
cmake --build --preset asan-headless
ctest --preset asan-headless
```

The headless preset disables SDL-dependent examples and the graphical client.
This keeps server, test, benchmark, replay, and SDK validation usable on build
machines without a graphics toolchain.

The repository's parity roadmap and acceptance gates are tracked in
[`docs/PARITY_PLAN.md`](docs/PARITY_PLAN.md).

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
