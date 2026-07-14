#!/usr/bin/env python3
"""Summarize macro benchmark CSVs and enforce the explicit release budgets."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int((len(ordered) - 1) * fraction + 0.999999)))
    return ordered[index]


def load_cases(path: Path) -> dict[str, list[dict[str, str]]]:
    cases: dict[str, list[dict[str, str]]] = defaultdict(list)
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            if row["case"].startswith("macro_"):
                cases[row["case"]].append(row)
    return cases


def load_metrics(path: Path) -> dict[str, dict[str, list[float]]]:
    metrics: dict[str, dict[str, list[float]]] = defaultdict(lambda: defaultdict(list))
    with path.open(newline="", encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            metrics[row["case"]][row["metric"]].append(float(row["value"]))
    return metrics


def p95(metrics: dict[str, dict[str, list[float]]], case: str, metric: str) -> float:
    return percentile(metrics.get(case, {}).get(metric, []), 0.95)


def median_case(cases: dict[str, list[dict[str, str]]], case: str) -> float:
    return statistics.median(float(row["elapsed_ms"]) for row in cases[case])


def print_summary(
    cases: dict[str, list[dict[str, str]]], metrics: dict[str, dict[str, list[float]]]
) -> None:
    print("\nmacro benchmark summary")
    print(f"{'case':42} {'median':>10} {'p95':>10} {'execute':>10} {'nodes':>12} {'rss MiB':>10}")
    for name in sorted(cases):
        elapsed = [float(row["elapsed_ms"]) for row in cases[name]]
        execute = p95(metrics, name, "macro.execute")
        nodes = max(metrics.get(name, {}).get("macro.generated_nodes", [0.0]))
        rss_mib = max(metrics.get(name, {}).get("macro.worker_rss", [0.0])) / 1024.0
        print(
            f"{name:42} {statistics.median(elapsed):9.3f}ms "
            f"{percentile(elapsed, 0.95):9.3f}ms {execute:9.3f}ms "
            f"{nodes:12.0f} {rss_mib:10.2f}"
        )


class BudgetChecks:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.failures.append(message)

    def maximum(self, actual: float, limit: float, label: str) -> None:
        self.require(actual <= limit, f"{label}: {actual:.3f} exceeds {limit:.3f}")

    def minimum(self, actual: float, limit: float, label: str) -> None:
        self.require(actual >= limit, f"{label}: {actual:.3f} is below {limit:.3f}")


def check_budgets(
    cases: dict[str, list[dict[str, str]]], metrics: dict[str, dict[str, list[float]]]
) -> list[str]:
    check = BudgetChecks()
    required = {
        *(f"macro_noop_{count}_execute" for count in (1, 100, 1000, 10000)),
        *(f"macro_debug_{count}_execute" for count in (1, 100, 1000, 10000)),
        *(f"macro_debug_{count}_cached" for count in (1, 100, 1000, 10000)),
        *(f"macro_debug_{count}_handwritten" for count in (1, 100, 1000, 10000)),
        *(f"macro_debug_fields_{count}_execute" for count in (10, 50, 200)),
        "macro_json_showcase_cached",
    }
    missing = sorted(required - cases.keys())
    check.require(not missing, "missing required cases: " + ", ".join(missing))

    volume_cases = [name for name in cases if name.startswith("macro_volume_")]
    generated_scales = sorted(
        max(metrics.get(name, {}).get("macro.generated_nodes", [0.0])) for name in volume_cases
    )
    for threshold in (1_000, 10_000, 100_000, 1_000_000):
        check.require(
            any(nodes >= threshold for nodes in generated_scales),
            f"missing expansion-volume case reaching {threshold} generated AST nodes",
        )

    if "macro_debug_1000_unrelated" in cases:
        unrelated = "macro_debug_1000_unrelated"
        check.maximum(p95(metrics, unrelated, "macro.executions"), 0.0, "unrelated edit executions")
        bookkeeping = sum(
            p95(metrics, unrelated, metric)
            for metric in ("macro.cache_read", "macro.validate", "macro.hygiene", "macro.merge")
        )
        frontend = percentile(
            [float(row["elapsed_ms"]) for row in cases[unrelated]], 0.95
        )
        check.maximum(bookkeeping, max(2.0, frontend * 0.03), "unrelated edit bookkeeping")

    if "macro_debug_1000_helper" in cases:
        executions = metrics.get("macro_debug_1000_helper", {}).get("macro.executions", [])
        check.require(bool(executions) and all(value == 1 for value in executions),
                      "helper edit must execute exactly one macro per sample")

    check.maximum(p95(metrics, "macro_debug_1_cached", "macro.cache_read"), 20.0,
                  "cached macro acquisition")
    check.maximum(p95(metrics, "macro_debug_1_execute", "macro.execute"), 5.0,
                  "simple Debug execution")
    check.maximum(p95(metrics, "macro_debug_fields_200_execute", "macro.execute"), 20.0,
                  "200-field Debug execution")
    check.maximum(p95(metrics, "macro_noop_1000_execute", "macro.execute"), 100.0,
                  "1,000 no-op executions")
    check.maximum(p95(metrics, "macro_debug_1000_execute", "macro.execute"), 250.0,
                  "1,000 Debug executions")

    if "macro_debug_1000_cached" in cases and "macro_debug_1000_handwritten" in cases:
        ratio = median_case(cases, "macro_debug_1000_cached") / max(
            median_case(cases, "macro_debug_1000_handwritten"), 0.001
        )
        check.maximum(ratio, 1.20, "cached macro/handwritten frontend ratio")

    for name in volume_cases:
        nodes = max(metrics.get(name, {}).get("macro.generated_nodes", [0.0]))
        transport_ms = (
            p95(metrics, name, "macro.protocol")
            + p95(metrics, name, "macro.validate")
            + p95(metrics, name, "macro.merge")
        )
        if nodes > 10_000 and transport_ms > 0:
            check.minimum(nodes / (transport_ms / 1000.0), 250_000,
                          f"{name} expansion transport nodes/s")

    observed_rss = max(
        (value for case in metrics.values() for value in case.get("macro.worker_rss", [])),
        default=0.0,
    )
    check.maximum(observed_rss / 1024.0, 64.0, "macro worker RSS MiB")
    return check.failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("cases", type=Path)
    parser.add_argument("metrics", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    cases = load_cases(args.cases)
    metrics = load_metrics(args.metrics)
    print_summary(cases, metrics)
    if not args.check:
        return 0
    failures = check_budgets(cases, metrics)
    if failures:
        print("\nmacro release budget failures:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print("\nmacro release budgets passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
