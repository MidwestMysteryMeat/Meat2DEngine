#!/usr/bin/env python3
"""Generate release checksums and a minimal CycloneDX SBOM for CPack output."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import uuid
from pathlib import Path


DEPENDENCIES = (
    {
        "name": "SDL",
        "version": "release-3.4.10",
        "url": "https://github.com/libsdl-org/SDL",
        "license": "Zlib",
    },
    {
        "name": "SDL_image",
        "version": "release-3.4.4",
        "url": "https://github.com/libsdl-org/SDL_image",
        "license": "Zlib",
    },
    {
        "name": "Dear ImGui",
        "version": "v1.92.8",
        "url": "https://github.com/ocornut/imgui",
        "license": "MIT",
    },
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="release version")
    parser.add_argument("--platform", required=True, help="artifact platform")
    parser.add_argument("--package-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--include-client-dependencies",
        action="store_true",
        help="include SDL and Dear ImGui when the package includes client targets",
    )
    return parser.parse_args()


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_checksums(archives: list[Path], output: Path) -> None:
    lines = [f"{checksum(path)}  {path.name}" for path in sorted(archives)]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_sbom(
    version: str, platform: str, output: Path, include_client_dependencies: bool
) -> None:
    dependencies = DEPENDENCIES if include_client_dependencies else ()
    components = [
        {
            "type": "library",
            "name": dependency["name"],
            "version": dependency["version"],
            "purl": f"pkg:github/{dependency['url'].split('github.com/', 1)[1]}@{dependency['version']}",
            "licenses": [{"license": {"name": dependency["license"]}}],
            "externalReferences": [{"type": "vcs", "url": dependency["url"]}],
        }
        for dependency in dependencies
    ]
    document = {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": (
            "urn:uuid:"
            + str(
                uuid.uuid5(
                    uuid.NAMESPACE_URL,
                    f"https://github.com/MidwestMysteryMeat/Meat2DEngine/{version}/{platform}",
                )
            )
        ),
        "version": 1,
        "metadata": {
            "timestamp": "1970-01-01T00:00:00Z",
            "tools": [{"vendor": "Midwest Mystery Meat", "name": "release_metadata.py"}],
            "component": {
                "type": "application",
                "name": "Meat2DEngine",
                "version": version,
                "purl": f"pkg:github/MidwestMysteryMeat/Meat2DEngine@{version}",
                "externalReferences": [
                    {
                        "type": "vcs",
                        "url": "https://github.com/MidwestMysteryMeat/Meat2DEngine",
                    }
                ],
            },
        },
        "components": components,
        "properties": [
            {"name": "meat2d:platform", "value": platform},
            {"name": "meat2d:source-revision", "value": os.environ.get("GITHUB_SHA", "unknown")},
        ],
    }
    output.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    archives = sorted(
        path
        for pattern in ("*.zip", "*.tar.gz")
        for path in args.package_dir.glob(pattern)
        if path.is_file()
    )
    if not archives:
        raise SystemExit(f"no CPack archives found in {args.package_dir}")
    write_checksums(archives, args.output_dir / f"Meat2D-{args.version}-{args.platform}-SHA256SUMS.txt")
    write_sbom(
        args.version,
        args.platform,
        args.output_dir / f"Meat2D-{args.version}-{args.platform}.cdx.json",
        args.include_client_dependencies,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
