# Compression and packed assets plan

Meat2D needs compression in three different places, with different safety and
latency requirements:

| Area | Goal | Default policy |
| --- | --- | --- |
| Offline asset/package build | Smaller distributables and faster installs | Compress eligible source/data assets in a deterministic pack step |
| Persistence and world streaming | Reduce save/chunk I/O without making recovery fragile | Compress independently addressed chunks and snapshots after validation |
| Multiplayer transport | Reduce chunk/scene bandwidth | Compress before fragmentation, within an explicit decompression budget |

Compression is lossless storage/transport support. It is not a replacement for
texture, audio, or shader formats that already contain their own compression.
PNG/JPEG/WebP, Ogg/Opus/MP3, archives, and encrypted data should normally be
copied or stored without a second compression pass.

When confidentiality is required, follow the separate
[encryption and key-management plan](ENCRYPTION_PLAN.md): compress first,
encrypt second, and authenticate the package metadata before parsing.

## Reference review

### LZAV — preferred candidate for evaluation

[`avaneev/lzav`](https://github.com/avaneev/lzav) is a portable C/C++
header-only LZ77 implementation under MIT. Its README describes bounded
out-of-bounds checks during decompression, support for malformed/damaged input,
and a requirement that callers provide the original output size. It reports
fast in-memory compression/decompression and supports blocks up to 2 GiB, while
recommending larger chunks for very large data.

That makes it a plausible candidate for packed data, save chunks, and cache
blocks, but not an automatic choice. Meat2D must pin a reviewed version, keep
the license/notice in the distribution, benchmark the actual target CPUs, and
wrap it behind an engine codec interface. The original-size requirement is a
useful safety property: the wrapper can reject an output size before allocating.

### Huffman repositories — references, not dependencies

[`AnshulRanjan2004/File-Compression-Utility`](https://github.com/AnshulRanjan2004/File-Compression-Utility)
documents a teaching-oriented Huffman implementation and is GPL-3.0. Its
repository describes an ASCII-oriented 128-node design. It should not be
copied into the permissively licensed engine SDK without a separate licensing
decision and a binary-safe rewrite.

[`sspeedy99/File-Compression`](https://github.com/sspeedy99/File-Compression)
is MIT and documents a simple Huffman file format, but its own README lists
binary-file and Unicode support as future work. It is useful for format and
round-trip test ideas, not as the engine's runtime codec.

## Proposed engine contract

Add a codec-neutral `meat2d::compression` API with:

1. `CodecId` (`None`, first reviewed LZAV integration, and future codecs).
2. A versioned envelope containing magic, codec ID, flags, uncompressed size,
   compressed size, content hash/checksum, and optional dictionary ID.
3. `compress()` and `decompress()` operations that require caller-supplied
   maximum sizes and return failure rather than throwing on malformed input.
4. Independent block boundaries. A damaged asset, save chunk, or network block
   must not require decoding an entire package.
5. Deterministic compression settings for reproducible packages and save
   fixtures. Runtime decompression must not depend on thread count.

The wrapper must reject zero/overflowed sizes, mismatched lengths, unknown
codec/version values, output sizes above the configured budget, truncated data,
and decompression expansion beyond the declared size. Integrity verification
must happen on the uncompressed bytes before they are handed to a parser or
simulation subsystem.

## Implementation phases

### Compression 0 — contract and benchmark

- [x] Add the codec interface, envelope schema, size limits, and failure
      behavior for bounded raw/LZAV blocks.
- [x] Add a third-party notice/pinning policy and pin LZAV revision
      `ebe3b58aea896e8ce6db6d7ddbb11dffced281e4`; benchmark selection remains.
- [x] Benchmark raw, LZAV, and no-compression paths on representative scene,
      tile-map, JSON/TOML metadata, world-chunk, snapshot, and incompressible
      payloads with `meat2d_compression_benchmark`.
- [ ] Define thresholds where storing raw data is cheaper or faster.

### Compression 1 — assets and package build

- [x] Add the initial deterministic asset-pack output with sorted normalized
      entries, bounded payloads, per-entry checksums, codec metadata, and
      random-access reads.
- [ ] Add explicit alignment, a signed manifest, package-level hashes, and
      release migration metadata.
- [ ] Compress text/data/scene/tile content and leave already-compressed media
      raw unless measurements prove otherwise.
- [ ] Add editor cache invalidation by source hash and package consumer tests.
- [ ] Add package size, cold-start, random-access, and corrupted-entry tests.

### Compression 2 — persistence and streaming

- [ ] Compress individual `ChunkStore` chunk files and session snapshot blocks
      behind the existing generation-manifest commit protocol.
- [ ] Verify decompressed size/hash before replacing an active generation.
- [ ] Add background I/O and bounded decompression work; never decode on the
      authoritative tick if an asynchronous path can be used.
- [ ] Add migration fixtures for raw, compressed, and future codec versions.

### Compression 3 — networking

- [ ] Compress chunk/scene payloads before fragmentation and enforce both
      compressed datagram limits and uncompressed expansion limits.
- [ ] Add codec capability negotiation and a raw fallback for incompatible
      peers; never assume compression is available from the protocol version.
- [ ] Skip compression for small or incompressible payloads based on measured
      thresholds, and retain end-to-end hashes after decompression.
- [ ] Add loss/reordering, malformed-block, decompression-bomb, and CPU-budget
      tests before enabling it for Internet-facing sessions.

### Compression 4 — release and operations

- [ ] Publish compression ratios, CPU cost, memory cost, and package-size
      regressions in CI/benchmark artifacts.
- [ ] Include third-party license notices and codec version metadata in every
      SDK/package archive.
- [ ] Document cache invalidation, save compatibility, and downgrade behavior.

## Explicit non-goals

- Do not use Huffman or neural-network compression as the authoritative runtime
  default without binary-safe formats, benchmarks, fuzzing, and a license
  review.
- Do not compress encrypted payloads; encryption should be applied after any
  intended compression, with decompression budgets enforced before decryption
  output enters a parser.
- Do not make compressed assets the only representation until raw fallback and
  migration tooling are tested.
