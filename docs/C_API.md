# C ABI

`include/meat2d/c_api.h` is the first versioned C ABI surface. It is designed
for language bindings and runtime hosts that cannot consume the C++20 API.

The ABI currently exposes an opaque, deterministic cellular `World` handle:

- create/destroy with validated dimensions and sleep budget;
- dimensions, tick, state-hash, and material reads/writes;
- one deterministic step with C-compatible tick statistics;
- bounded RGBA raster output;
- numeric status codes instead of exceptions or C++ types across the boundary.

`MEAT2D_C_API_VERSION` is independent of the C++ serialization versions. A
consumer should check `meat2d_c_api_version()` and treat unknown status values
as failures. The ABI does not yet expose scenes, networking, editor services,
scripts, or ownership-transfer callbacks; those are added only after their
budgets and compatibility contracts are stable.
