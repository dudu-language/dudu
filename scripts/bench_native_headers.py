#!/usr/bin/env python3
"""Measure cold and cached native-header awareness without entering the fast test loop."""

import argparse
import csv
import os
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--build-dir", default="build-release")
    parser.add_argument("--csv", default="build/bench_compiler/native_headers.csv")
    parser.add_argument("--no-build", action="store_true")
    return parser.parse_args()


def pkg_exists(package: str) -> bool:
    return subprocess.run(
        ["pkg-config", "--exists", package], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    ).returncode == 0


def prepare_libcxx_case(root: Path) -> tuple[Path, bool]:
    clang = os.environ.get("CLANGXX", "clang++")
    supported = subprocess.run(
        [clang, "-std=c++20", "-stdlib=libc++", "-x", "c++", "-fsyntax-only", "-"],
        input=b"#include <vector>\n",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    ).returncode == 0
    project = root / "build/bench_compiler/native_libcxx"
    if supported:
        project.mkdir(parents=True, exist_ok=True)
        (project / "main.dd").write_text(
            (root / "tests/fixtures/std_vector_map_string.dd").read_text()
        )
        (project / "dudu.toml").write_text(
            'main = "main.dd"\ncpp_std = "c++20"\n\n[cc]\nflags = ["-stdlib=libc++"]\n'
        )
    return project / "main.dd", supported


def timed_check(duc: Path, source: Path) -> tuple[float, float, float, int]:
    time_tool = Path("/usr/bin/time")
    if not time_tool.is_file():
        start = time.perf_counter_ns()
        subprocess.run([duc, "check", source], check=True)
        return (time.perf_counter_ns() - start) / 1e6, 0.0, 0.0, 0

    with tempfile.NamedTemporaryFile() as metrics:
        start = time.perf_counter_ns()
        subprocess.run(
            [
                time_tool,
                "-f",
                "%e,%U,%S,%M",
                "-o",
                metrics.name,
                duc,
                "check",
                source,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        wall_ms = (time.perf_counter_ns() - start) / 1e6
        metrics.seek(0)
        _, user, system, rss = metrics.read().decode().strip().split(",")
    return wall_ms, float(user) * 1000.0, float(system) * 1000.0, int(rss)


def main() -> int:
    args = parse_args()
    if args.samples < 1:
        raise SystemExit("--samples must be at least 1")

    root = Path(__file__).resolve().parent.parent
    build_dir = (root / args.build_dir).resolve()
    duc = build_dir / "duc"
    if not args.no_build:
        subprocess.run(["cmake", "--build", build_dir, "-j", "--target", "duc"], check=True)
    if not duc.is_file():
        raise SystemExit(f"missing compiler: {duc}")

    libcxx_source, libcxx_enabled = prepare_libcxx_case(root)
    cases = [
        ("small", root / "tests/fixtures/array_c_handoff.dd", True),
        ("stl", root / "tests/fixtures/std_vector_map_string.dd", True),
        ("stl_libcxx", libcxx_source, libcxx_enabled),
        ("stl_large", root / "tests/fixtures/cpp_stdlib_interop.dd", True),
        ("sdl3", root / "examples/sdl3_window.dd", pkg_exists("sdl3")),
        ("raylib", root / "examples/raylib_game.dd", pkg_exists("raylib")),
    ]

    rows = []
    for name, source, enabled in cases:
        if not enabled:
            print(f"skip {name}: optional native package unavailable")
            continue
        for mode in ("cold", "cached"):
            for sample in range(1, args.samples + 1):
                if mode == "cold":
                    subprocess.run(
                        [duc, "clean-cache", source, "--quiet"],
                        check=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                else:
                    subprocess.run(
                        [duc, "check", source],
                        check=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                wall_ms, user_ms, system_ms, rss_kb = timed_check(duc, source)
                rows.append((name, mode, sample, wall_ms, user_ms, system_ms, rss_kb))

    for name, _, enabled in cases:
        if not enabled:
            continue
        for mode in ("cold", "cached"):
            values = [row[3] for row in rows if row[0] == name and row[1] == mode]
            print(
                f"{name:10} {mode:6} median={statistics.median(values):8.1f} ms "
                f"range={min(values):.1f}-{max(values):.1f}"
            )

    csv_path = (root / args.csv).resolve()
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    with csv_path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("case", "mode", "sample", "wall_ms", "user_ms", "system_ms", "rss_kb"))
        writer.writerows(rows)
    print(csv_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
