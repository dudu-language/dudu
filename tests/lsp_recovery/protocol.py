import json
import subprocess


def encode(value):
    body = json.dumps(value, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"


def request(request_id, method, params):
    return encode({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})


def notification(method, params):
    return encode({"jsonrpc": "2.0", "method": method, "params": params})


def decode(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            raise AssertionError("truncated LSP response headers")
        length = None
        for line in data[cursor:header_end].decode().split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
        if length is None:
            raise AssertionError("LSP response omitted Content-Length")
        body_start = header_end + 4
        body_end = body_start + length
        if body_end > len(data):
            raise AssertionError("truncated LSP response body")
        messages.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return messages


def position(source, needle, add=0, occurrence=0):
    offset = -1
    for _ in range(occurrence + 1):
        offset = source.index(needle, offset + 1)
    offset += add
    before = source[:offset]
    return {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1])}


def text_document(uri):
    return {"uri": uri}


def response(messages, request_id):
    item = next((item for item in messages if item.get("id") == request_id), None)
    if item is None:
        raise AssertionError(f"missing response {request_id}")
    if "error" in item:
        raise AssertionError(f"request {request_id} failed: {item['error']}")
    return item.get("result")


def run_server(repo_root, messages, timeout=30):
    process = subprocess.run(
        [str(repo_root / "build" / "dudu-lsp")],
        input="".join(messages).encode(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )
    if process.returncode != 0 or process.stderr:
        raise AssertionError(process.stderr.decode(errors="replace"))
    return decode(process.stdout)
