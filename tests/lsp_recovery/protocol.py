import json
import os
import select
import subprocess
import time


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


class LspSession:
    def __init__(self, repo_root):
        self.process = subprocess.Popen(
            [str(repo_root / "build" / "dudu-lsp")],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        self.buffer = b""
        self.messages = []

    def send(self, message):
        self.process.stdin.write(message.encode())
        self.process.stdin.flush()

    def receive_until(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            message = self._next_message(deadline - time.monotonic())
            self.messages.append(message)
            if predicate(message):
                return message
        raise AssertionError("timed out waiting for LSP message")

    def request(self, request_id, method, params, timeout=10):
        self.send(request(request_id, method, params))
        message = self.receive_until(lambda item: item.get("id") == request_id, timeout)
        if "error" in message:
            raise AssertionError(f"request {request_id} failed: {message['error']}")
        return message.get("result")

    def close(self, request_id):
        self.request(request_id, "shutdown", None)
        self.send(notification("exit", None))
        self.process.stdin.close()
        return_code = self.process.wait(timeout=10)
        stderr = self.process.stderr.read().decode(errors="replace")
        if return_code != 0 or stderr:
            raise AssertionError(stderr or f"dudu-lsp exited with {return_code}")

    def _next_message(self, timeout):
        deadline = time.monotonic() + timeout
        while True:
            header_end = self.buffer.find(b"\r\n\r\n")
            if header_end >= 0:
                length = None
                for line in self.buffer[:header_end].decode().split("\r\n"):
                    if line.lower().startswith("content-length:"):
                        length = int(line.split(":", 1)[1].strip())
                if length is None:
                    raise AssertionError("LSP response omitted Content-Length")
                body_start = header_end + 4
                body_end = body_start + length
                if len(self.buffer) >= body_end:
                    body = self.buffer[body_start:body_end]
                    self.buffer = self.buffer[body_end:]
                    return json.loads(body)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError("timed out reading LSP response")
            ready, _, _ = select.select([self.process.stdout], [], [], remaining)
            if not ready:
                raise AssertionError("timed out reading LSP response")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                stderr = self.process.stderr.read().decode(errors="replace")
                raise AssertionError(stderr or "dudu-lsp closed its output")
            self.buffer += chunk
