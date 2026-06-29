#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

python3 - "$repo_root" <<'PY'
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile

repo_root = pathlib.Path(sys.argv[1]).resolve()
duc = repo_root / "build" / "duc"


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


tmp = pathlib.Path(tempfile.mkdtemp(prefix="dudu_lsp_matrix_"))
try:
    (tmp / "math_utils.dd").write_text(
        """# Magic value used by matrix fixture docs.
MAGIC: i32 = 9

def mix(left: i32, right: i32) -> i32:
    '''Mixes two numbers for signature docs.'''
    return left + right + MAGIC
"""
    )
    (tmp / "transitive.dd").write_text(
        """def transitive_value() -> i32:
    return 5
"""
    )
    (tmp / "entities.dd").write_text(
        """'''Entities module docs.'''

import transitive

# Max health for player docs.
MAX_HP: i32 = 42

enum Mode:
    '''Mode enum docs.'''
    Play
    Pause

enum OtherMode:
    Play

enum Token:
    Eof
    IntLit(i64)

class Box[T]:
    value: T

    def get(self) -> T:
        return self.value

def identity[T](value: T) -> T:
    return value

class Player:
    '''Runtime player docs.'''
    # Current hit points docs.
    hp: i32

    def move(self, dx: i32, dy: i32) -> i32:
        '''Moves the player docs.'''
        self.hp += dx + dy
        return self.hp

    @operator("==")
    def equals(self, other: Player) -> bool:
        return self.hp == other.hp

class Enemy:
    hp: i32

    def hurt(self) -> i32:
        return self.hp
"""
    )
    main_source = """import math_utils as math
import entities
import transitive
from entities import Player
from entities import MAX_HP
from entities import Mode
from entities import Token
from entities import Box
from entities import identity

def main() -> i32:
    player: Player = Player(MAX_HP)
    player.move(2, 3)
    current = player.hp
    mode: Mode = Mode.Play
    token: Token = Token.IntLit(i64(41))
    score = math.mix(current, identity[i32](1))
    box: Box[i32] = Box[i32](score)
    same = player == Player(MAX_HP)
    if same:
        return box.get() + transitive.transitive_value()
    return math.MAGIC
"""
    (tmp / "main.dd").write_text(main_source)
    ops_source = """class Vec2:
    x: i32
    y: i32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)

def main() -> i32:
    left: Vec2 = Vec2(1, 2)
    right: Vec2 = Vec2(3, 4)
    total = left + right
    return total.x
"""
    (tmp / "operators.dd").write_text(ops_source)
    (tmp / "native_bridge.h").write_text(
        """#pragma once

#define DUDU_MATRIX_NATIVE_MAGIC 12
#define DUDU_MATRIX_NATIVE_SCALE(value) ((value) * 3)

typedef struct MatrixNativePoint {
    int x;
    int y;
} MatrixNativePoint;

int matrix_native_add(int a, int b);
"""
    )
    native_source = """import c "native_bridge.h" as nb

def main() -> i32:
    point: nb.MatrixNativePoint
    point.x = nb.DUDU_MATRIX_NATIVE_MAGIC
    return nb.matrix_native_add(point.x, nb.DUDU_MATRIX_NATIVE_SCALE(2))
"""
    (tmp / "native_user.dd").write_text(native_source)
    missing_source = """import definitely_missing

def main() -> i32:
    return 0
"""
    (tmp / "missing_import.dd").write_text(missing_source)

    main = tmp / "main.dd"
    entities = tmp / "entities.dd"
    entities_source = entities.read_text()
    ops = tmp / "operators.dd"
    native = tmp / "native_user.dd"
    missing = tmp / "missing_import.dd"
    messages = [
        lsp_message({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": tmp.as_uri()}}),
        lsp_message({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
        open_message(main),
        open_message(entities),
        open_message(ops),
        open_message(native),
        open_message(missing),
        lsp_message({"jsonrpc": "2.0", "method": "textDocument/didSave", "params": {"textDocument": text_document(missing)}}),
        request(10, "textDocument/documentSymbol", {"textDocument": text_document(main)}),
        request(18, "textDocument/documentSymbol", {"textDocument": text_document(entities)}),
        request(11, "workspace/symbol", {"query": "Player"}),
        request(12, "workspace/symbol", {"query": "Mode"}),
        request(13, "workspace/symbol", {"query": "Token"}),
        request(14, "workspace/symbol", {"query": "Box"}),
        request(15, "workspace/symbol", {"query": "MAX_HP"}),
        request(16, "workspace/symbol", {"query": "mix"}),
        request(17, "workspace/symbol", {"query": "add"}),
        request(20, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(21, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=1)}),
        request(22, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(23, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(24, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "MAX_HP", occurrence=1)}),
        request(25, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(26, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(27, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(28, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Box[i32]", add=1)}),
        request(29, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "transitive.transitive_value", add=len("transitive."))}),
        request(30, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "current", occurrence=1)}),
        request(31, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player: Player", add=1)}),
        request(32, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "current =")}),
        request(33, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "math.MAGIC", add=len("math."))}),
        request(34, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "transitive.transitive_value", add=len("transitive."))}),
        request(35, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(36, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "math.mix(current", add=len("math.mix(current"))}),
        request(37, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "import entities", add=len("import "))}),
        request(38, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "hp: i32", add=1)}),
        request(39, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "Play", add=1)}),
        request(42, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(40, "textDocument/documentSymbol", {"textDocument": text_document(ops)}),
        request(41, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "add(self", add=1)}),
        request(50, "textDocument/completion", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(51, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.MatrixNativePoint", add=len("nb."))}),
        request(52, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(53, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(54, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(99, "shutdown", None),
        lsp_message({"jsonrpc": "2.0", "method": "exit", "params": None}),
    ]
    proc = subprocess.run(
        [str(duc.parent / "dudu-lsp")],
        input="".join(messages).encode(),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=10,
        check=False,
    )
    if proc.returncode != 0:
        raise AssertionError(proc.stderr.decode(errors="replace"))
    messages = read_lsp_messages(proc.stdout)

    assert_symbol_names(response(messages, 10), ["main"])
    entity_symbols = response(messages, 18)
    assert_symbol_names(entity_symbols, ["MAX_HP", "Mode", "Box", "Player"])
    if "Runtime player docs." not in item_named(entity_symbols, "Player").get("detail", ""):
        raise AssertionError(f"missing Player doc detail in {entity_symbols!r}")
    if "Moves the player docs." not in item_named(entity_symbols, "move").get("detail", ""):
        raise AssertionError(f"missing move doc detail in {entity_symbols!r}")
    if "Mode enum docs." not in item_named(entity_symbols, "Mode").get("detail", ""):
        raise AssertionError(f"missing Mode doc detail in {entity_symbols!r}")
    for request_id in range(11, 18):
        assert_nonempty(response(messages, request_id), f"workspace symbol {request_id}")
    for request_id in range(20, 30):
        assert_nonempty(response(messages, request_id), f"definition {request_id}")
    for request_id in (30, 31):
        hover = response(messages, request_id)
        assert_nonempty(hover and hover.get("contents"), f"hover {request_id}")
    assert_nonempty(response(messages, 32), "local references")
    assert_completion_labels(response(messages, 33), ["MAGIC", "mix"])
    assert_documentation_contains(item_named(response(messages, 33), "mix"), "Mixes two numbers")
    assert_completion_labels(response(messages, 34), ["transitive_value"])
    member_completion = response(messages, 35)
    assert_completion_labels(member_completion, ["move", "hp"])
    assert_documentation_contains(item_named(member_completion, "move"), "Moves the player docs.")
    signature_help = response(messages, 36)
    signature_docs = signature_help["signatures"][0]["documentation"]["value"]
    if "Mixes two numbers for signature docs." not in signature_docs:
        raise AssertionError(f"missing signature docs: {signature_help!r}")
    module_hover = response(messages, 37)
    module_hover_value = module_hover["contents"]["value"]
    if "Entities module docs." not in module_hover_value:
        raise AssertionError(f"missing module docs: {module_hover!r}")
    member_refs = response(messages, 38)
    assert_nonempty(member_refs, "member identity references")
    player_self_hp = position(entities_source, "self.hp", add=len("self."))
    enemy_self_hp = position(entities_source, "self.hp", occurrence=3, add=len("self."))
    if not has_start(member_refs, entities.as_uri(), player_self_hp["line"], player_self_hp["character"]):
        raise AssertionError(f"missing Player.self hp reference: {member_refs!r}")
    if has_start(member_refs, entities.as_uri(), enemy_self_hp["line"], enemy_self_hp["character"]):
        raise AssertionError(f"Enemy.self hp reference leaked into Player.hp refs: {member_refs!r}")
    enum_refs = response(messages, 39)
    assert_nonempty(enum_refs, "enum value identity references")
    mode_play = position(entities_source, "Play")
    other_play = position(entities_source, "Play", occurrence=1)
    if not has_start(enum_refs, entities.as_uri(), mode_play["line"], mode_play["character"]):
        raise AssertionError(f"missing Mode.Play declaration reference: {enum_refs!r}")
    if has_start(enum_refs, entities.as_uri(), other_play["line"], other_play["character"]):
        raise AssertionError(f"OtherMode.Play leaked into Mode.Play refs: {enum_refs!r}")
    member_use_refs = response(messages, 42)
    assert_nonempty(member_use_refs, "member use identity references")
    main_player_hp = position(main_source, "player.hp", add=len("player."))
    if not has_start(member_use_refs, main.as_uri(), main_player_hp["line"], main_player_hp["character"]):
        raise AssertionError(f"missing main player.hp use reference: {member_use_refs!r}")
    if not has_start(member_use_refs, entities.as_uri(), player_self_hp["line"], player_self_hp["character"]):
        raise AssertionError(f"missing Player.self hp through use-site refs: {member_use_refs!r}")
    if has_start(member_use_refs, entities.as_uri(), enemy_self_hp["line"], enemy_self_hp["character"]):
        raise AssertionError(f"Enemy.self hp leaked into use-site refs: {member_use_refs!r}")
    assert_symbol_names(response(messages, 40), ["Vec2", "main"])
    assert_nonempty(response(messages, 41), "operator method definition")
    assert_completion_labels(response(messages, 50), ["matrix_native_add", "MatrixNativePoint", "DUDU_MATRIX_NATIVE_SCALE"])
    for request_id in (51, 52, 53):
        assert_nonempty(response(messages, request_id), f"native request {request_id}")
    native_type_hover = response(messages, 51)["contents"]["value"]
    if "Native identity:" not in native_type_hover or "MatrixNativePoint" not in native_type_hover:
        raise AssertionError(f"missing native type identity: {native_type_hover!r}")
    native_macro_hover = response(messages, 53)["contents"]["value"]
    if "Native identity: `path:DUDU_MATRIX_NATIVE_SCALE`" not in native_macro_hover:
        raise AssertionError(f"missing native macro identity: {native_macro_hover!r}")
    assert_nonempty(response(messages, 54), "native function references")
    missing_diags = publish_diagnostics(messages, missing.as_uri())
    if not missing_diags or not missing_diags[-1]:
        raise AssertionError("missing import fixture did not publish diagnostics")

    print("lsp matrix checks passed")
finally:
    shutil.rmtree(tmp, ignore_errors=True)
PY
