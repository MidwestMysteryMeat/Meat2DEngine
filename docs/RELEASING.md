# Releasing Meat2D

1. Update `CHANGELOG.md`, migration notes, and the project version in
   `CMakeLists.txt`.
2. Run the headless build, focused tests, package-consumer test, and fuzz
   harness compile checks locally when the required toolchains are available.
3. Commit the release metadata and create an annotated tag such as `v0.4.0`.
4. Push the tag. `.github/workflows/release.yml` builds Linux and Windows
   CPack archives, installs/tests the SDK, generates the Doxygen API site,
   SHA-256 checksums, and a CycloneDX SBOM, and attaches all metadata to the
   GitHub release.
5. Verify the release assets and test a clean consumer project against the
   published archive before announcing the release.

The release workflow uses a headless SDK configuration so it does not bundle
platform-specific SDL binaries. Client and launcher builds remain available
from source and can be added to a future platform-specific distribution once
their runtime dependency policy is finalized.
