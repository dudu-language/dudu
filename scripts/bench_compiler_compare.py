#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
import os
import shutil
import statistics
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable


ROOT = Path(__file__).resolve().parents[1]


@dataclass
class CommandCase:
    language: str
    operation: str
    command: Callable[[int], list[str]]
    prepare: Callable[[int], None] = lambda _sample: None
    available: bool = True
    note: str = ""


@dataclass
class Sample:
    language: str
    operation: str
    sample: int
    elapsed_ms: float
    peak_rss_kb: int


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(len(ordered) * fraction + 0.999) - 1))
    return ordered[index]


def tool(name: str) -> str | None:
    return shutil.which(name)


def write_dudu(path: Path, units: int) -> None:
    lines: list[str] = []
    for i in range(units):
        lines.extend(
            [
                f"class Item{i}:",
                "    a: i32",
                "    b: i32",
                "",
                "    def total(self) -> i32:",
                "        return self.a + self.b",
                "",
                f"def use_{i}(value: Item{i}) -> i32:",
                "    return value.total()",
                "",
            ]
        )
    last = units - 1
    lines.extend(
        [
            "def main() -> i32:",
            f"    value = Item{last}(a=1, b=2)",
            f"    return use_{last}(value) - 3",
            "",
        ]
    )
    path.write_text("\n".join(lines))


def write_cpp(path: Path, units: int) -> None:
    lines = ["#include <cstdint>", ""]
    for i in range(units):
        lines.extend(
            [
                f"struct Item{i} {{",
                "    std::int32_t a;",
                "    std::int32_t b;",
                "    std::int32_t total() const { return a + b; }",
                "};",
                f"std::int32_t use_{i}(Item{i} value) {{ return value.total(); }}",
                "",
            ]
        )
    last = units - 1
    lines.append(f"int main() {{ return use_{last}(Item{last}{{1, 2}}) - 3; }}")
    path.write_text("\n".join(lines) + "\n")


def write_rust(path: Path, units: int) -> None:
    lines = ["#![allow(dead_code)]", ""]
    for i in range(units):
        lines.extend(
            [
                f"struct Item{i} {{ a: i32, b: i32 }}",
                f"impl Item{i} {{ fn total(&self) -> i32 {{ self.a + self.b }} }}",
                f"fn use_{i}(value: Item{i}) -> i32 {{ value.total() }}",
                "",
            ]
        )
    last = units - 1
    lines.append(f"fn main() {{ std::process::exit(use_{last}(Item{last} {{ a: 1, b: 2 }}) - 3); }}")
    path.write_text("\n".join(lines) + "\n")


def write_swift(path: Path, units: int) -> None:
    lines: list[str] = []
    for i in range(units):
        lines.extend(
            [
                f"struct Item{i} {{",
                "    let a: Int32",
                "    let b: Int32",
                "    func total() -> Int32 { a + b }",
                "}",
                f"func use_{i}(_ value: Item{i}) -> Int32 {{ value.total() }}",
                "",
            ]
        )
    last = units - 1
    lines.extend(
        [
            "@main",
            "struct Main {",
            "    static func main() {",
            f"        let result = use_{last}(Item{last}(a: 1, b: 2))",
            "        if result != 3 { fatalError() }",
            "    }",
            "}",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def write_nim(path: Path, units: int) -> None:
    lines: list[str] = []
    for i in range(units):
        lines.extend(
            [
                f"type Item{i} = object",
                "  a: int32",
                "  b: int32",
                f"proc total(value: Item{i}): int32 = value.a + value.b",
                f"proc use_{i}(value: Item{i}): int32 = value.total()",
                "",
            ]
        )
    last = units - 1
    lines.extend(
        [
            "when isMainModule:",
            f"  let result = use_{last}(Item{last}(a: 1, b: 2))",
            "  if result != 3: quit(1)",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def write_csharp(path: Path, units: int) -> None:
    lines = ["using System;", ""]
    for i in range(units):
        lines.extend(
            [
                f"readonly struct Item{i}",
                "{",
                "    public readonly int A;",
                "    public readonly int B;",
                f"    public Item{i}(int a, int b) {{ A = a; B = b; }}",
                "    public int Total() => A + B;",
                "}",
                "",
            ]
        )
    lines.extend(["static class Program", "{"])
    for i in range(units):
        lines.append(f"    static int Use{i}(Item{i} value) => value.Total();")
    last = units - 1
    lines.extend(
        [
            f"    static int Main() => Use{last}(new Item{last}(1, 2)) - 3;",
            "}",
        ]
    )
    path.write_text("\n".join(lines) + "\n")


def write_go(path: Path, units: int) -> None:
    lines = ["package main", ""]
    for i in range(units):
        lines.extend(
            [
                f"type Item{i} struct {{ a int32; b int32 }}",
                f"func (value Item{i}) total() int32 {{ return value.a + value.b }}",
                f"func use{i}(value Item{i}) int32 {{ return value.total() }}",
                "",
            ]
        )
    last = units - 1
    lines.append(f"func main() {{ _ = use{last}(Item{last}{{a: 1, b: 2}}) }}")
    path.write_text("\n".join(lines) + "\n")


def prepare_go_build(path: Path, units: int, sample: int) -> None:
    write_go(path, units)
    with path.open("a") as handle:
        handle.write(f"// benchmark sample {sample}\n")


def run_timed(command: list[str], output_dir: Path, label: str) -> tuple[float, int]:
    rss_path = output_dir / f"{label}.rss"
    stderr_path = output_dir / f"{label}.err"
    timed = ["/usr/bin/time", "-f", "%M", "-o", str(rss_path), *command]
    start = time.perf_counter()
    with stderr_path.open("w") as stderr:
        result = subprocess.run(timed, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=stderr)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    if result.returncode != 0:
        detail = stderr_path.read_text(errors="replace")
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}\n{detail}")
    return elapsed_ms, int(rss_path.read_text().strip())


def compiler_version(command: str, args: list[str]) -> str:
    if tool(command) is None:
        return "not installed"
    result = subprocess.run([command, *args], text=True, capture_output=True, check=False)
    text = (result.stdout or result.stderr).strip()
    return text.splitlines()[0] if text else "unknown"


def cases_for(work: Path, units: int) -> tuple[list[CommandCase], dict[str, str], dict[str, Path]]:
    src = work / "src"
    out = work / "out"
    src.mkdir(parents=True, exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    paths = {
        "dudu": src / "main.dd",
        "cpp": src / "main.cpp",
        "rust": src / "main.rs",
        "swift": src / "main.swift",
        "nim": src / "main.nim",
        "csharp": src / "Program.cs",
        "go": src / "main.go",
    }
    write_dudu(paths["dudu"], units)
    write_cpp(paths["cpp"], units)
    write_rust(paths["rust"], units)
    write_swift(paths["swift"], units)
    write_nim(paths["nim"], units)
    write_csharp(paths["csharp"], units)
    write_go(paths["go"], units)

    csproj = src / "CompilerCompare.csproj"
    csproj.write_text(
        '<Project Sdk="Microsoft.NET.Sdk">\n'
        '  <PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework>'
        '<ImplicitUsings>disable</ImplicitUsings><Nullable>disable</Nullable></PropertyGroup>\n'
        "</Project>\n"
    )
    if tool("dotnet"):
        subprocess.run(["dotnet", "restore", str(csproj)], cwd=ROOT, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    duc = ROOT / "build" / "duc"
    if not duc.exists():
        raise RuntimeError("build/duc is missing; build Dudu before running the matrix")
    cxx = tool("g++") or tool("c++")
    if cxx is None:
        raise RuntimeError("a C++ compiler is required")

    generated_cpp = out / "dudu.cpp"
    cases = [
        CommandCase("Dudu", "frontend", lambda _n: [str(duc), "check", str(paths["dudu"])]),
        CommandCase("Dudu", "emit", lambda _n: [str(duc), "emit", str(paths["dudu"]), "-o", str(generated_cpp)]),
        CommandCase("C++ (self-contained Dudu output)", "executable", lambda n: [cxx, "-std=c++20", "-O0", str(generated_cpp), "-o", str(out / f"dudu-{n}")], prepare=lambda _n: subprocess.run([str(duc), "emit", str(paths["dudu"]), "-o", str(generated_cpp)], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)),
        CommandCase("C++", "frontend", lambda _n: [cxx, "-std=c++20", "-fsyntax-only", str(paths["cpp"])]),
        CommandCase("C++", "executable", lambda n: [cxx, "-std=c++20", "-O0", str(paths["cpp"]), "-o", str(out / f"cpp-{n}")]),
        CommandCase("Rust", "frontend", lambda n: ["rustc", "--edition=2021", "--emit=metadata", str(paths["rust"]), "-o", str(out / f"rust-{n}.rmeta")], available=tool("rustc") is not None),
        CommandCase("Rust", "executable", lambda n: ["rustc", "--edition=2021", "-C", "opt-level=0", str(paths["rust"]), "-o", str(out / f"rust-{n}")], available=tool("rustc") is not None),
        CommandCase("Swift", "frontend", lambda _n: ["swiftc", "-parse-as-library", "-typecheck", str(paths["swift"])], available=tool("swiftc") is not None),
        CommandCase("Swift", "executable", lambda n: ["swiftc", "-parse-as-library", "-Onone", str(paths["swift"]), "-o", str(out / f"swift-{n}")], available=tool("swiftc") is not None),
        CommandCase("Nim", "frontend", lambda n: ["nim", "check", "--hints:off", "--warnings:off", f"--nimcache:{out / f'nim-check-{n}'}", str(paths["nim"])], available=tool("nim") is not None),
        CommandCase("Nim", "executable", lambda n: ["nim", "c", "--hints:off", "--warnings:off", f"--nimcache:{out / f'nim-build-{n}'}", f"--out:{out / f'nim-{n}'}", str(paths["nim"])], available=tool("nim") is not None),
        CommandCase("C#", "executable", lambda n: ["dotnet", "build", str(csproj), "--no-restore", "--no-incremental", "-c", "Debug", "-o", str(out / f"csharp-{n}")], available=tool("dotnet") is not None, note="MSBuild project build; no direct frontend row"),
        CommandCase("Go", "frontend", lambda n: ["go", "tool", "compile", "-o", str(out / f"go-{n}.o"), str(paths["go"])], available=tool("go") is not None),
        CommandCase("Go", "executable", lambda n: ["go", "build", "-gcflags=all=-N -l", "-o", str(out / f"go-{n}"), str(paths["go"])], prepare=lambda n: prepare_go_build(paths["go"], units, n), available=tool("go") is not None),
    ]
    versions = {
        "Dudu": subprocess.run([str(duc), "--version"], text=True, capture_output=True).stdout.strip(),
        "C++": compiler_version("g++", ["--version"]),
        "Rust": compiler_version("rustc", ["--version"]),
        "Swift": compiler_version("swiftc", ["--version"]),
        "Nim": compiler_version("nim", ["--version"]),
        "C#": compiler_version("dotnet", ["--version"]),
        "Go": compiler_version("go", ["version"]),
    }
    paths["dudu_generated_cpp"] = generated_cpp
    return cases, versions, paths


def source_stats(paths: dict[str, Path]) -> list[dict[str, object]]:
    labels = {
        "dudu": "Dudu",
        "dudu_generated_cpp": "Generated Dudu C++",
        "cpp": "C++",
        "rust": "Rust",
        "swift": "Swift",
        "nim": "Nim",
        "csharp": "C#",
        "go": "Go",
    }
    rows: list[dict[str, object]] = []
    for key, label in labels.items():
        path = paths[key]
        text = path.read_text()
        rows.append({"language": label, "lines": len(text.splitlines()), "bytes": len(text)})
    return rows


def summarize(samples: list[Sample]) -> list[dict[str, object]]:
    grouped: dict[tuple[str, str], list[Sample]] = {}
    for sample in samples:
        grouped.setdefault((sample.language, sample.operation), []).append(sample)
    rows: list[dict[str, object]] = []
    for (language, operation), values in grouped.items():
        elapsed = [value.elapsed_ms for value in values]
        rows.append(
            {
                "language": language,
                "operation": operation,
                "median_ms": statistics.median(elapsed),
                "p95_ms": percentile(elapsed, 0.95),
                "peak_rss_mib": max(value.peak_rss_kb for value in values) / 1024.0,
            }
        )
    return rows


def write_reports(work: Path, samples: list[Sample], rows: list[dict[str, object]],
                  versions: dict[str, str], sources: list[dict[str, object]],
                  commands: list[dict[str, str]], units: int) -> None:
    with (work / "samples.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=Sample.__dataclass_fields__.keys())
        writer.writeheader()
        writer.writerows(sample.__dict__ for sample in samples)
    report = {
        "units": units,
        "versions": versions,
        "sources": sources,
        "commands": commands,
        "results": rows,
    }
    (work / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    lines = [
        f"Reference workload: {units:,} generated declaration/function units.",
        "",
        "| Language | Operation | Median | p95 | Peak RSS |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    for row in rows:
        lines.append(
            f"| {row['language']} | {row['operation']} | {row['median_ms']:.1f} ms | "
            f"{row['p95_ms']:.1f} ms | {row['peak_rss_mib']:.1f} MiB |"
        )
    lines.extend([
        "",
        "Source sizes:",
        "",
        "| Language | Lines | Bytes |",
        "| --- | ---: | ---: |",
    ])
    for source in sources:
        lines.append(
            f"| {source['language']} | {source['lines']:,} | {source['bytes']:,} |"
        )
    lines.extend(["", "Toolchains:"])
    for language, version in versions.items():
        lines.append(f"- {language}: {version}")
    (work / "matrix.md").write_text("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--units", type=int, default=1000)
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "compiler_compare")
    args = parser.parse_args()
    if args.samples < 1 or args.units < 1:
        parser.error("--samples and --units must be positive")
    work = args.output.resolve()
    if work.exists():
        shutil.rmtree(work)
    cases, versions, paths = cases_for(work, args.units)
    commands = [
        {
            "language": case.language,
            "operation": case.operation,
            "command": " ".join(case.command(1)),
        }
        for case in cases
        if case.available
    ]
    samples: list[Sample] = []
    for case in cases:
        if not case.available:
            print(f"skip {case.language} {case.operation}: toolchain not installed")
            continue
        print(f"measure {case.language} {case.operation}")
        for sample_index in range(1, args.samples + 1):
            case.prepare(sample_index)
            elapsed_ms, peak_rss_kb = run_timed(
                case.command(sample_index), work, f"{case.language}-{case.operation}-{sample_index}"
            )
            samples.append(
                Sample(case.language, case.operation, sample_index, elapsed_ms, peak_rss_kb)
            )
    dudu_emit = {sample.sample: sample for sample in samples
                 if sample.language == "Dudu" and sample.operation == "emit"}
    dudu_backend = {sample.sample: sample for sample in samples
                    if sample.language == "C++ (self-contained Dudu output)" and
                    sample.operation == "executable"}
    for sample_index in sorted(dudu_emit.keys() & dudu_backend.keys()):
        emit = dudu_emit[sample_index]
        backend = dudu_backend[sample_index]
        samples.append(
            Sample("Dudu self-contained toolchain", "executable", sample_index,
                   emit.elapsed_ms + backend.elapsed_ms,
                   max(emit.peak_rss_kb, backend.peak_rss_kb))
        )
    rows = summarize(samples)
    write_reports(work, samples, rows, versions, source_stats(paths), commands, args.units)
    print((work / "matrix.md").read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
