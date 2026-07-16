import json
import pathlib


def lsp_message(obj):
    body = json.dumps(obj, separators=(",", ":"))
    return f"Content-Length: {len(body)}\r\n\r\n{body}"


def read_lsp_messages(data):
    messages = []
    cursor = 0
    while cursor < len(data):
        header_end = data.find(b"\r\n\r\n", cursor)
        if header_end < 0:
            break
        headers = data[cursor:header_end].decode()
        length = None
        for line in headers.split("\r\n"):
            if line.lower().startswith("content-length:"):
                length = int(line.split(":", 1)[1].strip())
                break
        if length is None:
            raise AssertionError(f"missing Content-Length in {headers!r}")
        body_start = header_end + 4
        body_end = body_start + length
        messages.append(json.loads(data[body_start:body_end]))
        cursor = body_end
    return messages


def position(text, needle, add=0, occurrence=0):
    cursor = -1
    for _ in range(occurrence + 1):
        cursor = text.index(needle, cursor + 1)
    cursor += add
    before = text[:cursor]
    return {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1])}


def text_document(path):
    return {"uri": pathlib.Path(path).resolve().as_uri()}


def open_message(path):
    path = pathlib.Path(path).resolve()
    return lsp_message(
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": path.as_uri(),
                    "languageId": "dudu",
                    "version": 1,
                    "text": path.read_text(),
                }
            },
        }
    )


def request(request_id, method, params):
    return lsp_message({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params})


def response(messages, request_id):
    for item in messages:
        if item.get("id") == request_id:
            if "error" in item:
                raise AssertionError(f"request {request_id} failed: {item['error']}")
            return item.get("result")
    raise AssertionError(f"missing response for request {request_id}")


def publish_diagnostics(messages, uri):
    diagnostics = []
    for item in messages:
        if item.get("method") == "textDocument/publishDiagnostics":
            params = item.get("params", {})
            if params.get("uri") == uri:
                diagnostics.append(params.get("diagnostics", []))
    return diagnostics


def assert_symbol_names(result, expected):
    names = {item.get("name") for item in result}
    missing = sorted(set(expected) - names)
    if missing:
        raise AssertionError(f"missing symbols {missing}; got {sorted(names)}")


def assert_nonempty(result, label):
    if not result:
        raise AssertionError(f"{label} returned no result: {result!r}")


def assert_completion_labels(result, expected):
    labels = {item.get("label") for item in result}
    missing = sorted(set(expected) - labels)
    if missing:
        raise AssertionError(f"missing completions {missing}; got {sorted(labels)}")


def item_with_title(result, title):
    for item in result:
        if item.get("title") == title:
            return item
    raise AssertionError(f"missing action {title}; got {[item.get('title') for item in result]}")


def item_named(result, name):
    for item in result:
        if item.get("name") == name or item.get("label") == name:
            return item
    raise AssertionError(f"missing item {name}; got {result!r}")


def assert_documentation_contains(item, text):
    documentation = item.get("documentation", {})
    if isinstance(documentation, dict):
        value = documentation.get("value", "")
    else:
        value = str(documentation)
    if text not in value:
        raise AssertionError(f"missing documentation {text!r} in {item!r}")


def has_start(result, uri, line, character):
    for item in result:
        if item.get("uri") != uri:
            continue
        start = item.get("range", {}).get("start", {})
        if start.get("line") == line and start.get("character") == character:
            return True
    return False


def split_lines(text):
    return text.splitlines()


def decode_semantic_tokens(source, data, legend):
    lines = split_lines(source)
    decoded = []
    line = 0
    character = 0
    for i in range(0, len(data), 5):
        delta_line, delta_start, length, token_type, modifiers = data[i : i + 5]
        line += delta_line
        character = character + delta_start if delta_line == 0 else delta_start
        text = ""
        if 0 <= line < len(lines):
            text = lines[line][character : character + length]
        decoded.append((text, legend[token_type], modifiers, line, character))
    return decoded


def has_semantic(decoded, text, token_type, modifiers):
    return any(
        item_text == text and item_type == token_type and item_modifiers == modifiers
        for item_text, item_type, item_modifiers, _, _ in decoded
    )


def has_semantic_type(decoded, text, token_type):
    return any(item_text == text and item_type == token_type for item_text, item_type, _, _, _ in decoded)


def modifier_mask(modifiers, name):
    return 1 << modifiers.index(name)
