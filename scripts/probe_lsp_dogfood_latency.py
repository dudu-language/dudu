#!/usr/bin/env python3
import json
import subprocess
import sys
import time
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
        self.proc = subprocess.Popen(
            [str(lsp_bin)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.next_id = 1
        self.root = root.resolve()
        init_id = self.request_id()
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


def probe_workspace(lsp_bin, name, root, entry, needles):
    entry_text = entry.read_text()
    session = LspSession(lsp_bin, root)
    rows = []
    try:
        session.open_document(entry)
        doc = {"textDocument": text_document(entry)}

        response, elapsed = session.request("textDocument/documentSymbol", doc)
        require_nonempty_result(response, f"{name} cold documentSymbol")
        rows.append((name, "cold_document_symbol", elapsed))

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
        rows.append((name, "warm_definition", elapsed))

        hover_pos = position_of(entry_text, needles["hover"], offset=1)
        response, elapsed = session.request("textDocument/hover", {**doc, "position": hover_pos})
        require_nonempty_result(response, f"{name} cold native hover")
        rows.append((name, "cold_native_hover", elapsed))

        response, elapsed = session.request("textDocument/hover", {**doc, "position": hover_pos})
        require_nonempty_result(response, f"{name} hover")
        rows.append((name, "warm_hover", elapsed))

        references_pos = position_of(entry_text, needles["references"], offset=1)
        response, elapsed = session.request(
            "textDocument/references",
            {**doc, "position": references_pos, "context": {"includeDeclaration": True}},
        )
        require_nonempty_result(response, f"{name} references")
        rows.append((name, "warm_references", elapsed))

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
        rows.append((name, "warm_completion", elapsed))

        response, elapsed = session.request("textDocument/semanticTokens/full", doc)
        require_nonempty_result(response, f"{name} semantic tokens")
        if not response["result"].get("data"):
            raise RuntimeError(f"{name} semantic tokens returned no data")
        rows.append((name, "warm_semantic_tokens", elapsed))
    finally:
        session.close()
    return rows


def main():
    repo_root = Path(__file__).resolve().parents[1]
    lsp_bin = repo_root / "build" / "dudu-lsp"
    if len(sys.argv) > 1:
        lsp_bin = Path(sys.argv[1])
    if not lsp_bin.exists():
        raise SystemExit(f"missing dudu-lsp binary: {lsp_bin}")

    workspaces = [
        (
            "raymarch-dd",
            Path("/home/vega/Coding/Graphics/raymarch-dd"),
            Path("/home/vega/Coding/Graphics/raymarch-dd/src/main.dd"),
            {
                "definition": "setup_monitor_rect()",
                "definition_uri_suffix": "/src/windowing.dd",
                "hover": "render_w =",
                "references": "render_w =",
                "completion": "SDL_",
            },
        ),
        (
            "dudu-webserver",
            Path("/home/vega/Coding/Web/dudu-webserver"),
            Path("/home/vega/Coding/Web/dudu-webserver/src/server.dd"),
            {
                "definition": "parse_request(raw)",
                "definition_uri_suffix": "/src/request.dd",
                "hover": "std.string",
                "references": "server",
                "completion": "sock.",
            },
        ),
    ]

    all_rows = []
    for name, root, entry, needles in workspaces:
        if not root.exists() or not entry.exists():
            print(f"skip {name}: workspace not found", file=sys.stderr)
            continue
        all_rows.extend(probe_workspace(lsp_bin, name, root, entry, needles))

    if not all_rows:
        raise SystemExit("no dogfood workspaces were available")

    width = max(len(row[1]) for row in all_rows)
    for workspace, phase, elapsed in all_rows:
        print(f"{workspace:15} {phase:{width}} {elapsed:8.3f} ms")


if __name__ == "__main__":
    main()
