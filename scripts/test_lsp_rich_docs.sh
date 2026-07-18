#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake --build "$ROOT_DIR/build" --target dudu-lsp -j"$(nproc)"
python3 "$ROOT_DIR/tests/lsp_rich_docs/run.py" "$ROOT_DIR"
