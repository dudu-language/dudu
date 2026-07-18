import json


def message(payload):
    body = json.dumps(payload, separators=(",", ":"))
    return f"Content-Length: {len(body.encode())}\r\n\r\n{body}"


def request(request_id, method, params):
    return message({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})


def notification(method, params):
    return message({"jsonrpc": "2.0", "method": method, "params": params})


def text_document(path):
    return {"uri": path.as_uri()}


def open_document(path):
    return notification(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": path.as_uri(),
                "languageId": "dudu",
                "version": 1,
                "text": path.read_text(),
            }
        },
    )


def position(source, needle, occurrence=0, add=0):
    start = -1
    for _ in range(occurrence + 1):
        start = source.index(needle, start + 1)
    start += add
    line_start = source.rfind("\n", 0, start) + 1
    return {"line": source.count("\n", 0, start), "character": start - line_start}


def read_messages(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            break
        header = data[cursor:header_end].decode()
        length = int(next(line.split(":", 1)[1] for line in header.split("\r\n") if line.lower().startswith("content-length:")))
        body_start = header_end + 4
        messages.append(json.loads(data[body_start:body_start + length]))
        cursor = body_start + length
    return messages


def response(messages, request_id):
    for item in messages:
        if item.get("id") == request_id:
            if "error" in item:
                raise AssertionError(f"request {request_id} failed: {item['error']!r}")
            return item.get("result")
    raise AssertionError(f"missing response {request_id}")
