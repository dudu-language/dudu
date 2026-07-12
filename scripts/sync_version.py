#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


SEMVER = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?$")


def load_json(path: Path) -> dict:
    with path.open(encoding="utf-8") as source:
        return json.load(source)


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Check or synchronize generated version metadata from VERSION."
    )
    parser.add_argument("--write", action="store_true", help="update generated metadata")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    toolchain_version = (repo / "VERSION").read_text(encoding="utf-8").strip()
    if not SEMVER.fullmatch(toolchain_version):
        raise SystemExit(f"invalid Dudu VERSION: {toolchain_version!r}")

    marketplace_version = toolchain_version.split("-", 1)[0]
    package_path = repo / "editors/vscode/package.json"
    lock_path = repo / "editors/vscode/package-lock.json"
    package = load_json(package_path)
    lock = load_json(lock_path)

    if args.write:
        package["version"] = marketplace_version
        package.setdefault("duduToolchain", {})["minimumVersion"] = toolchain_version
        lock["version"] = marketplace_version
        lock.setdefault("packages", {}).setdefault("", {})["version"] = marketplace_version
        write_json(package_path, package)
        write_json(lock_path, lock)

    errors = []
    if package.get("version") != marketplace_version:
        errors.append(
            f"{package_path}: expected version {marketplace_version}, got {package.get('version')}"
        )
    minimum_toolchain = package.get("duduToolchain", {}).get("minimumVersion")
    if minimum_toolchain != toolchain_version:
        errors.append(
            f"{package_path}: expected minimum Dudu toolchain {toolchain_version}, got "
            f"{minimum_toolchain}"
        )
    if lock.get("version") != marketplace_version:
        errors.append(
            f"{lock_path}: expected root version {marketplace_version}, got {lock.get('version')}"
        )
    lock_package_version = lock.get("packages", {}).get("", {}).get("version")
    if lock_package_version != marketplace_version:
        errors.append(
            f"{lock_path}: expected package version {marketplace_version}, got "
            f"{lock_package_version}"
        )

    if errors:
        raise SystemExit("\n".join(errors) + "\nrun scripts/sync_version.py --write")

    print(f"version ok: toolchain {toolchain_version}, extension {marketplace_version}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
