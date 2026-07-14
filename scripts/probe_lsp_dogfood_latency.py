#!/usr/bin/env python3
import argparse
import csv
import json
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path


def frame_message(obj):
    body = json.dumps(obj, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}".encode()


def read_message(stream):
    header = bytearray()
    while not header.endswith(b"\r\n\r\n"):
        chunk = stream.read(1)
        if not chunk:
            raise RuntimeError("LSP server closed stdout")
        header.extend(chunk)
    content_length = None
    for line in header[:-4].decode().split("\r\n"):
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
            break
    if content_length is None:
        raise RuntimeError(f"missing Content-Length in {header!r}")
    return json.loads(stream.read(content_length))


def send_message(proc, obj):
    proc.stdin.write(frame_message(obj))
    proc.stdin.flush()


def wait_response(proc, request_id):
    while True:
        message = read_message(proc.stdout)
        if message.get("id") == request_id:
            if "error" in message:
                raise RuntimeError(f"LSP request {request_id} failed: {message['error']}")
            return message


class LspSession:
    def __init__(self, lsp_bin, root):
        self.started_at = time.perf_counter()
        self.proc = subprocess.Popen(
            [str(lsp_bin)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.next_id = 1
        self.root = root.resolve()
        init_id = self.request_id()
        start = time.perf_counter()
        send_message(
            self.proc,
            {
                "jsonrpc": "2.0",
                "id": init_id,
                "method": "initialize",
                "params": {"rootUri": self.root.as_uri()},
            },
        )
        wait_response(self.proc, init_id)
        self.initialize_ms = (time.perf_counter() - start) * 1000.0
        send_message(self.proc, {"jsonrpc": "2.0", "method": "initialized", "params": {}})

    def request_id(self):
        value = self.next_id
        self.next_id += 1
        return value

    def request(self, method, params):
        request_id = self.request_id()
        start = time.perf_counter()
        send_message(
            self.proc,
            {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params},
        )
        response = wait_response(self.proc, request_id)
        return response, (time.perf_counter() - start) * 1000.0

    def open_document(self, path):
        text = path.read_text()
        send_message(
            self.proc,
            {
                "jsonrpc": "2.0",
                "method": "textDocument/didOpen",
                "params": {
                    "textDocument": {
                        "uri": path.resolve().as_uri(),
                        "languageId": "dudu",
                        "version": 1,
                        "text": text,
                    }
                },
            },
        )
        while True:
            message = read_message(self.proc.stdout)
            if message.get("method") != "textDocument/publishDiagnostics":
                continue
            params = message.get("params", {})
            if params.get("uri") == path.resolve().as_uri():
                return (time.perf_counter() - self.started_at) * 1000.0

    def peak_rss_kb(self):
        status = Path(f"/proc/{self.proc.pid}/status")
        if not status.exists():
            return 0
        for line in status.read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1])
        return 0

    def close(self):
        try:
            shutdown_id = self.request_id()
            send_message(
                self.proc,
                {"jsonrpc": "2.0", "id": shutdown_id, "method": "shutdown", "params": None},
            )
            wait_response(self.proc, shutdown_id)
            send_message(self.proc, {"jsonrpc": "2.0", "method": "exit", "params": None})
        finally:
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def position_of(text, needle, offset=0):
    index = text.find(needle)
    if index < 0:
        raise RuntimeError(f"could not find {needle!r}")
    index += offset
    line = text.count("\n", 0, index)
    line_start = text.rfind("\n", 0, index)
    character = index if line_start < 0 else index - line_start - 1
    return {"line": line, "character": character}


def text_document(path):
    return {"uri": path.resolve().as_uri()}


def require_nonempty_result(response, label):
    result = response.get("result")
    if result is None:
        raise RuntimeError(f"{label} returned null")
    if isinstance(result, list) and not result:
        raise RuntimeError(f"{label} returned an empty list")
    if isinstance(result, dict) and not result:
        raise RuntimeError(f"{label} returned an empty object")


def result_locations(response):
    result = response.get("result")
    if result is None:
        return []
    if isinstance(result, list):
        return result
    return [result]


def require_definition_target(response, label, expected_uri_suffix):
    if not expected_uri_suffix:
        return
    for location in result_locations(response):
        if location.get("uri", "").endswith(expected_uri_suffix):
            return
    raise RuntimeError(
        f"{label} did not jump to {expected_uri_suffix}: {response.get('result')!r}"
    )


def probe_workspace(lsp_bin, name, root, entry, needles, sample):
    entry_text = entry.read_text()
    session = LspSession(lsp_bin, root)
    rows = []
    try:
        rows.append((name, sample, "initialize", session.initialize_ms))
        workspace_usable_ms = session.open_document(entry)
        rows.append((name, sample, "cold_workspace_usable", workspace_usable_ms))
        doc = {"textDocument": text_document(entry)}

        response, elapsed = session.request("textDocument/documentSymbol", doc)
        require_nonempty_result(response, f"{name} post-diagnostics documentSymbol")
        rows.append((name, sample, "post_diagnostics_document_symbol", elapsed))

        definition_pos = position_of(entry_text, needles["definition"], offset=1)
        response, elapsed = session.request(
            "textDocument/definition",
            {**doc, "position": definition_pos},
        )
        require_nonempty_result(response, f"{name} definition")
        require_definition_target(
            response,
            f"{name} definition",
            needles.get("definition_uri_suffix", ""),
        )
        rows.append((name, sample, "warm_definition", elapsed))

        hover_pos = position_of(entry_text, needles["native_hover"], offset=1)
        response, elapsed = session.request("textDocument/hover", {**doc, "position": hover_pos})
        require_nonempty_result(response, f"{name} cold native hover")
        rows.append((name, sample, "cold_native_hover", elapsed))

        response, elapsed = session.request("textDocument/hover", {**doc, "position": hover_pos})
        require_nonempty_result(response, f"{name} hover")
        rows.append((name, sample, "warm_hover", elapsed))

        references_pos = position_of(entry_text, needles["references"], offset=1)
        response, elapsed = session.request(
            "textDocument/references",
            {**doc, "position": references_pos, "context": {"includeDeclaration": True}},
        )
        require_nonempty_result(response, f"{name} references")
        rows.append((name, sample, "warm_references", elapsed))

        completion_pos = position_of(
            entry_text,
            needles["completion"],
            offset=len(needles["completion"]),
        )
        response, elapsed = session.request(
            "textDocument/completion",
            {**doc, "position": completion_pos},
        )
        require_nonempty_result(response, f"{name} completion")
        rows.append((name, sample, "warm_completion", elapsed))

        response, elapsed = session.request("textDocument/semanticTokens/full", doc)
        require_nonempty_result(response, f"{name} semantic tokens")
        if not response["result"].get("data"):
            raise RuntimeError(f"{name} semantic tokens returned no data")
        rows.append((name, sample, "warm_semantic_tokens", elapsed))

        rename_pos = position_of(entry_text, needles["rename"], offset=1)
        response, elapsed = session.request(
            "textDocument/rename",
            {
                **doc,
                "position": rename_pos,
                "newName": "dudu_latency_probe_name",
            },
        )
        require_nonempty_result(response, f"{name} rename")
        rows.append((name, sample, "warm_rename", elapsed))

        peak_rss_kb = session.peak_rss_kb()
    finally:
        session.close()
    return rows, peak_rss_kb


def percentile(values, fraction):
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * fraction + 0.999999))
    return ordered[index]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("lsp_bin", nargs="?", type=Path)
    parser.add_argument("--samples", type=int, default=1)
    parser.add_argument("--csv", type=Path)
    args = parser.parse_args()
    if args.samples < 1:
        raise SystemExit("--samples must be at least 1")

    repo_root = Path(__file__).resolve().parents[1]
    lsp_bin = args.lsp_bin or repo_root / "build" / "dudu-lsp"
    if not lsp_bin.exists():
        raise SystemExit(f"missing dudu-lsp binary: {lsp_bin}")

    workspaces = [
        (
            "raymarch-dd",
            Path("/home/vega/Coding/LangDev/Dudu/dogfooding/raymarch-dd"),
            Path("/home/vega/Coding/LangDev/Dudu/dogfooding/raymarch-dd/src/main.dd"),
            {
                "definition": "setup_monitor_rect()",
                "definition_uri_suffix": "/src/windowing.dd",
                "native_hover": "SDL_CreateWindow",
                "references": "render_w =",
                "completion": "SDL_",
                "rename": "setup_monitor_rect()",
            },
        ),
        (
            "dudu-webserver",
            Path("/home/vega/Coding/LangDev/Dudu/dogfooding/dudu-webserver"),
            Path("/home/vega/Coding/LangDev/Dudu/dogfooding/dudu-webserver/src/server.dd"),
            {
                "definition": "parse_request(raw)",
                "definition_uri_suffix": "/src/request.dd",
                "native_hover": "std.string",
                "references": "server",
                "completion": "sock.",
                "rename": "parse_request(raw)",
            },
        ),
    ]

    all_rows = []
    rss_rows = []
    for name, root, entry, needles in workspaces:
        if not root.exists() or not entry.exists():
            print(f"skip {name}: workspace not found", file=sys.stderr)
            continue
        for sample in range(1, args.samples + 1):
            rows, peak_rss_kb = probe_workspace(
                lsp_bin, name, root, entry, needles, sample
            )
            all_rows.extend(rows)
            rss_rows.append((name, sample, peak_rss_kb))

    if not all_rows:
        raise SystemExit("no dogfood workspaces were available")

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.writer(handle)
            writer.writerow(["workspace", "sample", "phase", "elapsed_ms", "peak_rss_kb"])
            rss_by_sample = {(workspace, sample): rss for workspace, sample, rss in rss_rows}
            for workspace, sample, phase, elapsed in all_rows:
                writer.writerow(
                    [workspace, sample, phase, f"{elapsed:.6f}", rss_by_sample[workspace, sample]]
                )

    grouped = defaultdict(list)
    for workspace, _sample, phase, elapsed in all_rows:
        grouped[(workspace, phase)].append(elapsed)
    width = max(len(phase) for _, phase in grouped)
    for (workspace, phase), values in sorted(grouped.items()):
        rss = max(value for name, _sample, value in rss_rows if name == workspace)
        print(
            f"{workspace:15} {phase:{width}} "
            f"median={statistics.median(values):8.3f} ms "
            f"p95={percentile(values, 0.95):8.3f} ms rss={rss / 1024.0:7.1f} MiB"
        )
    if args.csv:
        print(f"csv: {args.csv}")


if __name__ == "__main__":
    main()
