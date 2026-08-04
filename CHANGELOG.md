# Changelog

All notable changes to Meat2D are recorded here. The project follows
semantic-versioning conventions for tagged releases.

## [Unreleased]

- Added optional Clang/libFuzzer targets for packet, fragmentation, replay,
  sprite metadata, and project-path parsing.
- Split scene hierarchy, world diagnostics/reactions, discovery, living-agent,
  and project-template implementations into focused translation units.
- Added the release metadata tool used to produce SHA-256 checksums and a
  CycloneDX SBOM for CPack archives.
- Added tag-driven Linux and Windows package publishing automation.

## [0.4.0] - 2026-08-04

- Established the general-purpose 2D engine direction: side-scroller,
  top-down/RTS, RPG, visual-novel, metroidvania, shooter, sandbox, and
  destructible-terrain workflows are separate template families.
- Added deterministic fixed-step simulation, chunked worlds, dirty-region
  rendering data, replay verification, persistence primitives, authoritative
  networking, discovery, and project-management tooling.
- Added CMake SDK targets, install/export support, CPack archives, headless
  presets, sanitizer presets, tests, benchmarks, and package-consumer checks.

[Unreleased]: https://github.com/MidwestMysteryMeat/Meat2DEngine/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/MidwestMysteryMeat/Meat2DEngine/releases/tag/v0.4.0
