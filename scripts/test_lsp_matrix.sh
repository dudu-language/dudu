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
    (tmp / "other_math_utils.dd").write_text(
        """MAGIC: i32 = 99

def mix(left: i32, right: i32) -> i32:
    return left - right + MAGIC
"""
    )
    containers_source = """def count(values: list[i32]) -> i32:
    return 0
"""
    containers = tmp / "containers.dd"
    containers.write_text(containers_source)
    (tmp / "transitive.dd").write_text(
        """def transitive_value() -> i32:
    return 5
"""
    )
    (tmp / "entities.dd").write_text(
        """'''Entities module docs.'''

import transitive

# Player id alias docs.
type PlayerId = i32

# Max health for player docs.
MAX_HP: i32 = 42

enum Mode:
    '''Mode enum docs.'''
    # Mode play docs.
    Play
    Pause

enum OtherMode:
    Play

enum Token:
    Eof
    # Integer token docs.
    IntLit(i64)

enum OtherToken:
    IntLit(i64)

# Generic box docs.
class Box[T]:
    # Box value docs.
    value: T

    # Gets the boxed value docs.
    def get(self) -> T:
        return self.value

    def echo[U](self, value: U) -> U:
        '''Echoes a generic method value docs.'''
        return value

# Returns a generic identity value docs.
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

    def move(self, dx: i32, dy: i32) -> i32:
        self.hp -= dx + dy
        return self.hp

    def hurt(self) -> i32:
        return self.hp

class Counter:
    # Counter limit docs.
    LIMIT: i32 = 10
    # Counter mutable count docs.
    count: static[i32] = 0

    def bump() -> i32:
        '''Bumps the counter docs.'''
        Counter.count += 1
        return Counter.count

class OtherCounter:
    LIMIT: i32 = 900
    count: static[i32] = 0

    def bump() -> i32:
        OtherCounter.count += 10
        return OtherCounter.count
"""
    )
    (tmp / "other_entities.dd").write_text(
        """MAX_HP: i32 = 9001
"""
    )
    main_source = """import math_utils as math
import other_math_utils as other_math
import entities
import other_entities
import transitive
from math_utils import mix
from entities import Player
from entities import MAX_HP
from entities import Mode
from entities import Token
from entities import Box
from entities import identity
from entities import PlayerId
from entities import Counter

def main() -> i32:
    player: Player = Player(MAX_HP)
    player_id: PlayerId = 7
    player.move(2, 3)
    current = player.hp
    mode: Mode = Mode.Play
    token: Token = Token.IntLit(i64(41))
    score = math.mix(current, identity[i32](1))
    other_score = other_math.mix(other_entities.MAX_HP, other_math.MAGIC)
    counter_score = Counter.bump() + Counter.count + Counter.LIMIT
    box: Box[i32] = Box[i32](score)
    echoed = box.echo[i32](score)
    same = player == Player(MAX_HP)
    if same:
        return box.get() + echoed + transitive.transitive_value() + other_score + counter_score
    return math.MAGIC
"""
    (tmp / "main.dd").write_text(main_source)
    ops_source = """class Vec2:
    x: i32
    y: i32

    @operator("+")
    def add(self, other: Vec2) -> Vec2:
        '''Adds two Vec2 values docs.'''
        return Vec2(self.x + other.x, self.y + other.y)

def main() -> i32:
    left: Vec2 = Vec2(1, 2)
    right: Vec2 = Vec2(3, 4)
    total = left + right
    numbers: list[i32] = []
    numbers.append(1)
    if len(numbers) > 0:
        total = total + Vec2(numbers.back(), numbers.front())
    j = 0
    while j < 1:
        j += 1
    for i in range(2):
        numbers.append(i)
        total = total + Vec2(i, i)
    return total.x
"""
    (tmp / "operators.dd").write_text(ops_source)
    (tmp / "native_bridge.h").write_text(
        """#pragma once

/** Native scale macro docs. */
#define DUDU_MATRIX_NATIVE_SCALE(value) ((value) * 3)
#define DUDU_MATRIX_NATIVE_MAGIC 12

typedef struct MatrixNativePoint {
    /** Native point x coordinate docs. */
    int x;
    /** Native point y coordinate docs. */
    int y;
} MatrixNativePoint;

typedef enum MatrixNativeMode {
    /** Native mode fast docs. */
    MATRIX_MODE_FAST = 7,
    MATRIX_MODE_SLOW = 8
} MatrixNativeMode;

/** Adds two matrix fixture integers. */ int matrix_native_add(int a, int b);
"""
    )
    native_source = """import c "native_bridge.h" as nb

def main() -> i32:
    point: nb.MatrixNativePoint
    wrapped: *const[nb.MatrixNativePoint]
    point.x = nb.DUDU_MATRIX_NATIVE_MAGIC
    return nb.matrix_native_add(point.x, nb.DUDU_MATRIX_NATIVE_SCALE(2)) + nb.MATRIX_MODE_FAST
"""
    (tmp / "native_user.dd").write_text(native_source)
    (tmp / "needs_c_context.h").write_text(
        """#pragma once

struct DuduNeedsContext {
    size_t count;
    int state;
};
"""
    )
    native_context_source = """import c "./needs_c_context.h" as native

def main() -> i32:
    value: struct DuduNeedsContext
    value.count = 7
    value.state = 35
    return i32(value.count) + value.state
"""
    (tmp / "native_context_user.dd").write_text(native_context_source)
    (tmp / "native_widget.hpp").write_text(
        """#pragma once

/** Matrix widget class docs. */
class MatrixWidget {
  public:
    /** Builds a matrix widget from a seed. */
    explicit MatrixWidget(int seed) : value(seed) {}

    /** Scales the matrix widget by a factor. */
    int scaled(int factor) const {
        return value * factor;
    }

    /** Current widget value. */
    int value = 0;
};
"""
    )
    (tmp / "native_other_widget.hpp").write_text(
        """#pragma once

class OtherWidget {
  public:
    int scaled(int factor) const {
        return factor;
    }

    int value = 0;
};
"""
    )
    native_cpp_source = """import cpp "native_widget.hpp"

def main() -> i32:
    widget: MatrixWidget = MatrixWidget(5)
    widget.value = 6
    return widget.scaled(2)
"""
    (tmp / "native_cpp_user.dd").write_text(native_cpp_source)
    native_cpp_same_source = """import cpp "native_widget.hpp"

def same() -> i32:
    widget: MatrixWidget
    return widget.scaled(3) + widget.value
"""
    (tmp / "native_cpp_same.dd").write_text(native_cpp_same_source)
    native_cpp_other_source = """import cpp "native_other_widget.hpp"

def other() -> i32:
    widget: OtherWidget
    return widget.scaled(4) + widget.value
"""
    (tmp / "native_cpp_other.dd").write_text(native_cpp_other_source)
    (tmp / "native_namespace.hpp").write_text(
        """#pragma once

/** Matrix namespace docs. */
namespace matrix_space {
    /** Adds inside a native namespace. */
    inline int namespaced_add(int left, int right) {
        return left + right;
    }

    /** Returns a native template identity value. */
    template <typename T> T identity(T value) {
        return value;
    }
}
"""
    )
    native_namespace_source = """import cpp "native_namespace.hpp"

def main() -> i32:
    return matrix_space.namespaced_add(2, 3) + matrix_space.identity[i32](4)
"""
    (tmp / "native_namespace_user.dd").write_text(native_namespace_source)
    native_namespace_same_source = """import cpp "native_namespace.hpp"

def same_namespace() -> i32:
    return matrix_space.namespaced_add(4, 5)
"""
    (tmp / "native_namespace_same.dd").write_text(native_namespace_same_source)
    (tmp / "native_namespace_other.hpp").write_text(
        """#pragma once

namespace matrix_space {
    inline int namespaced_add(int left, int right) {
        return left - right;
    }
}
"""
    )
    native_namespace_other_source = """import cpp "native_namespace_other.hpp"

def other_namespace() -> i32:
    return matrix_space.namespaced_add(6, 7)
"""
    (tmp / "native_namespace_other.dd").write_text(native_namespace_other_source)
    unresolved_source = """class Player:
    hp: i32

    def move(self) -> i32:
        return self.hp

def main() -> i32:
    local_value = 1
    local_value
    player: Player = Player(3)
    player.move()
    missing_obj.field
    missing_call()
    return missing_value
"""
    (tmp / "unresolved_tokens.dd").write_text(unresolved_source)
    missing_source = """import definitely_missing

def main() -> i32:
    return 0
"""
    (tmp / "missing_import.dd").write_text(missing_source)
    available_symbol_source = """class MissingThing:
    value: i32
"""
    (tmp / "available_symbol.dd").write_text(available_symbol_source)
    missing_symbol_source = """def main() -> i32:
    value: MissingThing = MissingThing(1)
    return value.value
"""
    (tmp / "missing_symbol.dd").write_text(missing_symbol_source)
    (tmp / "alpha.dd").write_text(
        """def alpha() -> i32:
    return 1
"""
    )
    (tmp / "zeta.dd").write_text(
        """def zeta() -> i32:
    return 2
"""
    )
    disorganized_imports_source = """import zeta
import alpha

def main() -> i32:
    return alpha.alpha() + zeta.zeta()
"""
    (tmp / "disorganized_imports.dd").write_text(disorganized_imports_source)
    lint_quickfix_source = """def main() -> i32:
    unused = 1
    return 0
"""
    (tmp / "lint_quickfix.dd").write_text(lint_quickfix_source)

    main = tmp / "main.dd"
    entities = tmp / "entities.dd"
    entities_source = entities.read_text()
    ops = tmp / "operators.dd"
    native = tmp / "native_user.dd"
    native_context = tmp / "native_context_user.dd"
    native_cpp = tmp / "native_cpp_user.dd"
    native_cpp_same = tmp / "native_cpp_same.dd"
    native_cpp_other = tmp / "native_cpp_other.dd"
    native_namespace = tmp / "native_namespace_user.dd"
    native_namespace_same = tmp / "native_namespace_same.dd"
    native_namespace_other = tmp / "native_namespace_other.dd"
    unresolved = tmp / "unresolved_tokens.dd"
    missing = tmp / "missing_import.dd"
    available_symbol = tmp / "available_symbol.dd"
    missing_symbol = tmp / "missing_symbol.dd"
    alpha = tmp / "alpha.dd"
    zeta = tmp / "zeta.dd"
    disorganized_imports = tmp / "disorganized_imports.dd"
    lint_quickfix = tmp / "lint_quickfix.dd"
    missing_symbol_diag_range = {
        "start": position(missing_symbol_source, "MissingThing"),
        "end": position(missing_symbol_source, "MissingThing", add=len("MissingThing")),
    }
    lint_unused_range = {
        "start": position(lint_quickfix_source, "unused = 1"),
        "end": position(lint_quickfix_source, "unused = 1", add=len("unused")),
    }
    lint_unused_fix_range = {"start": {"line": 1, "character": 0}, "end": {"line": 2, "character": 0}}
    messages = [
        lsp_message({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"rootUri": tmp.as_uri()}}),
        lsp_message({"jsonrpc": "2.0", "method": "initialized", "params": {}}),
        open_message(main),
        open_message(tmp / "math_utils.dd"),
        open_message(containers),
        open_message(entities),
        open_message(ops),
        open_message(native),
        open_message(native_context),
        open_message(native_cpp),
        open_message(native_cpp_same),
        open_message(native_cpp_other),
        open_message(native_namespace),
        open_message(native_namespace_same),
        open_message(native_namespace_other),
        open_message(unresolved),
        open_message(missing),
        open_message(available_symbol),
        open_message(missing_symbol),
        open_message(alpha),
        open_message(zeta),
        open_message(disorganized_imports),
        open_message(lint_quickfix),
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
        request(78, "workspace/symbol", {"query": "Counter.count"}),
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
        request(49, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "player_id: PlayerId", add=len("player_id: "))}),
        request(125, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "from math_utils import mix", add=len("from "))}),
        request(126, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "from math_utils import mix", add=len("from math_utils import "))}),
        request(68, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(69, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "Counter.LIMIT", add=len("Counter."))}),
        request(30, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "current", occurrence=1)}),
        request(31, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player: Player", add=1)}),
        request(145, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player: Player", add=len("player: "))}),
        request(71, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(72, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(32, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "current =")}),
        request(66, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(67, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player("))}),
        request(73, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Counter.count", add=len("Counter."))}),
        request(74, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(111, "textDocument/prepareRename", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math."))}),
        request(79, "textDocument/rename", {"textDocument": text_document(main), "position": position(main_source, "math.mix", add=len("math.")), "newName": "blend"}),
        request(80, "textDocument/rename", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player(")), "newName": "START_HP"}),
        request(33, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "return math.MAGIC", add=len("return math."))}),
        request(34, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "transitive.transitive_value", add=len("transitive."))}),
        request(35, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(75, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump", add=len("Counter."))}),
        request(119, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(120, "textDocument/completion", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(76, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "Counter.bump()", add=len("Counter.bump("))}),
        request(77, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "Player(MAX_HP)", add=len("Player("))}),
        request(36, "textDocument/signatureHelp", {"textDocument": text_document(main), "position": position(main_source, "math.mix(current", add=len("math.mix(current"))}),
        request(37, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "import entities", add=len("import "))}),
        request(38, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "hp: i32", add=1)}),
        request(39, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "    Play", add=5)}),
        request(42, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(43, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "player.move", add=len("player."))}),
        request(44, "textDocument/references", {"textDocument": text_document(entities), "position": position(entities_source, "move(self")}),
        request(45, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player.hp", add=len("player."))}),
        request(46, "textDocument/semanticTokens/full", {"textDocument": text_document(main)}),
        request(84, "textDocument/semanticTokens/full", {"textDocument": text_document(native)}),
        request(85, "textDocument/semanticTokens/full", {"textDocument": text_document(entities)}),
        request(47, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Mode.Play", add=len("Mode."))}),
        request(48, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "player_id: PlayerId", add=len("player_id: "))}),
        request(62, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(130, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "def main() -> i32", add=len("def main() -> "))}),
        request(132, "textDocument/hover", {"textDocument": text_document(containers), "position": position(containers_source, "list[i32]", add=1)}),
        request(63, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "Token.IntLit", add=len("Token."))}),
        request(40, "textDocument/documentSymbol", {"textDocument": text_document(ops)}),
        request(142, "textDocument/semanticTokens/full", {"textDocument": text_document(ops)}),
        request(131, "textDocument/definition", {"textDocument": text_document(tmp / "math_utils.dd"), "position": position((tmp / "math_utils.dd").read_text(), "mix(left", add=1)}),
        request(41, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "add(self", add=1)}),
        request(143, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "@operator", add=1)}),
        request(144, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "@operator", add=1)}),
        request(146, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=1)}),
        request(147, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=len("self."))}),
        request(148, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=1)}),
        request(149, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=len("other."))}),
        request(150, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "self.x + other.x", add=1)}),
        request(151, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "other.x", add=1)}),
        request(152, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "for i in range", add=len("for "))}),
        request(153, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "range(2)", add=1)}),
        request(154, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "numbers.append(1)", add=len("numbers."))}),
        request(155, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "if len", add=1)}),
        request(156, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "while j", add=1)}),
        request(157, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "len(numbers)", add=1)}),
        request(158, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "numbers.back()", add=len("numbers."))}),
        request(94, "textDocument/definition", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(95, "textDocument/hover", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(96, "textDocument/references", {"textDocument": text_document(ops), "position": position(ops_source, "left + right", add=len("left "))}),
        request(97, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(98, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "identity[i32]", add=1)}),
        request(99, "textDocument/definition", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(100, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(101, "textDocument/references", {"textDocument": text_document(main), "position": position(main_source, "box.echo", add=len("box."))}),
        request(102, "textDocument/hover", {"textDocument": text_document(main), "position": position(main_source, "Box[i32]", add=1)}),
        request(50, "textDocument/completion", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(51, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.MatrixNativePoint", add=len("nb."))}),
        request(52, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(53, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(54, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(128, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "\"native_bridge.h\"", add=1)}),
        request(129, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, " as nb", add=len(" as "))}),
        request(121, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(122, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))}),
        request(123, "textDocument/signatureHelp", {"textDocument": text_document(native), "position": position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE(2)", add=len("nb.DUDU_MATRIX_NATIVE_SCALE("))}),
        request(124, "textDocument/documentSymbol", {"textDocument": text_document(native)}),
        request(55, "textDocument/signatureHelp", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add(point.x", add=len("nb.matrix_native_add(point.x"))}),
        request(112, "textDocument/prepareRename", {"textDocument": text_document(native), "position": position(native_source, "nb.matrix_native_add", add=len("nb."))}),
        request(91, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(92, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(93, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))}),
        request(127, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "wrapped: *const[nb.MatrixNativePoint]", add=len("wrapped: *const[nb."))}),
        request(116, "textDocument/hover", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(117, "textDocument/definition", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(118, "textDocument/references", {"textDocument": text_document(native), "position": position(native_source, "point.x", add=len("point."))}),
        request(113, "textDocument/definition", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(114, "textDocument/hover", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(115, "textDocument/references", {"textDocument": text_document(native_context), "position": position(native_context_source, "value.count", add=len("value."))}),
        request(56, "textDocument/completion", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(57, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(58, "textDocument/signatureHelp", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled(2", add=len("widget.scaled(2"))}),
        request(59, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(60, "textDocument/references", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.value", add=len("widget."))}),
        request(61, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget: MatrixWidget", add=len("widget: "))}),
        request(64, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled", add=len("widget."))}),
        request(65, "textDocument/references", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "widget.scaled", add=len("widget."))}),
        request(81, "textDocument/signatureHelp", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=len("MatrixWidget("))}),
        request(82, "textDocument/definition", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=1)}),
        request(83, "textDocument/hover", {"textDocument": text_document(native_cpp), "position": position(native_cpp_source, "MatrixWidget(5)", add=1)}),
        request(86, "textDocument/hover", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(87, "textDocument/definition", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(88, "textDocument/completion", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=len("matrix_space."))}),
        request(89, "textDocument/semanticTokens/full", {"textDocument": text_document(native_namespace)}),
        request(90, "textDocument/references", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.namespaced_add", add=1)}),
        request(103, "textDocument/hover", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(104, "textDocument/definition", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(105, "textDocument/references", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))}),
        request(106, "textDocument/signatureHelp", {"textDocument": text_document(native_namespace), "position": position(native_namespace_source, "matrix_space.identity[i32](4)", add=len("matrix_space.identity[i32]("))}),
        request(107, "textDocument/codeAction", {
            "textDocument": text_document(missing_symbol),
            "range": missing_symbol_diag_range,
            "context": {
                "diagnostics": [{
                    "range": missing_symbol_diag_range,
                    "severity": 1,
                    "source": "dudu/sema",
                    "code": "dudu.sema.unknown_identifier",
                    "message": "unknown identifier: MissingThing",
                    "data": {"name": "MissingThing"},
                }]
            },
        }),
        request(108, "textDocument/codeAction", {
            "textDocument": text_document(disorganized_imports),
            "range": {"start": {"line": 0, "character": 0}, "end": {"line": 1, "character": len("import alpha")}},
            "context": {"diagnostics": []},
        }),
        request(109, "textDocument/codeAction", {
            "textDocument": text_document(lint_quickfix),
            "range": lint_unused_range,
            "context": {
                "diagnostics": [{
                    "range": lint_unused_range,
                    "severity": 2,
                    "source": "dudu/lint",
                    "code": "dudu.lint.unused",
                    "message": "unused local: unused",
                    "data": {"fixRange": lint_unused_fix_range},
                }]
            },
        }),
        request(110, "completionItem/resolve", {
            "label": "mix",
            "kind": 3,
            "detail": "def mix(left: i32, right: i32) -> i32",
            "documentation": {
                "kind": "markdown",
                "value": "Mixes two numbers for signature docs.",
            },
        }),
        request(70, "textDocument/semanticTokens/full", {"textDocument": text_document(unresolved)}),
        request(900, "shutdown", None),
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
    counter_workspace_symbols = response(messages, 78)
    assert_symbol_names(counter_workspace_symbols, ["Counter.count"])
    if "Counter mutable count docs." not in item_named(counter_workspace_symbols, "Counter.count").get("detail", ""):
        raise AssertionError(f"missing Counter.count workspace symbol docs: {counter_workspace_symbols!r}")
    for request_id in range(20, 30):
        assert_nonempty(response(messages, request_id), f"definition {request_id}")
    player_definition = response(messages, 21)
    player_decl = position(entities_source, "class Player")
    if player_definition["uri"] != entities.as_uri():
        raise AssertionError(f"Player constructor definition did not jump to source module: {player_definition!r}")
    if player_definition["range"]["start"]["line"] != player_decl["line"]:
        raise AssertionError(f"Player constructor definition jumped to wrong line: {player_definition!r}")
    from_module_definition = response(messages, 125)
    if from_module_definition["uri"] != (tmp / "math_utils.dd").as_uri():
        raise AssertionError(f"from-import module token did not jump to module file: {from_module_definition!r}")
    if from_module_definition["range"]["start"]["line"] != 0:
        raise AssertionError(f"from-import module token did not jump to module top: {from_module_definition!r}")
    from_symbol_definition = response(messages, 126)
    math_source = (tmp / "math_utils.dd").read_text()
    math_mix_decl = position(math_source, "mix(left")
    if from_symbol_definition["uri"] != (tmp / "math_utils.dd").as_uri():
        raise AssertionError(f"from-import symbol token did not jump to symbol module: {from_symbol_definition!r}")
    if from_symbol_definition["range"]["start"]["line"] != math_mix_decl["line"]:
        raise AssertionError(f"from-import symbol token did not jump to symbol declaration: {from_symbol_definition!r}")
    for request_id in (30, 31):
        hover = response(messages, request_id)
        assert_nonempty(hover and hover.get("contents"), f"hover {request_id}")
    assert_nonempty(response(messages, 32), "local references")
    imported_function_refs = response(messages, 66)
    assert_nonempty(imported_function_refs, "imported Dudu function references")
    other_math_source = (tmp / "other_math_utils.dd").read_text()
    other_math_mix_decl = position(other_math_source, "mix(left")
    main_math_mix = position(main_source, "math.mix", add=len("math."))
    main_other_math_mix = position(main_source, "other_math.mix", add=len("other_math."))
    if not has_start(imported_function_refs, (tmp / "math_utils.dd").as_uri(), math_mix_decl["line"], math_mix_decl["character"]):
        raise AssertionError(f"missing math.mix declaration reference: {imported_function_refs!r}")
    if not has_start(imported_function_refs, main.as_uri(), main_math_mix["line"], main_math_mix["character"]):
        raise AssertionError(f"missing math.mix use reference: {imported_function_refs!r}")
    if has_start(imported_function_refs, (tmp / "other_math_utils.dd").as_uri(), other_math_mix_decl["line"], other_math_mix_decl["character"]):
        raise AssertionError(f"other_math.mix declaration leaked into math.mix refs: {imported_function_refs!r}")
    if has_start(imported_function_refs, main.as_uri(), main_other_math_mix["line"], main_other_math_mix["character"]):
        raise AssertionError(f"other_math.mix use leaked into math.mix refs: {imported_function_refs!r}")
    imported_function_rename = response(messages, 79)
    rename_changes = imported_function_rename.get("changes", {})
    math_edits = rename_changes.get((tmp / "math_utils.dd").as_uri(), [])
    main_edits = rename_changes.get(main.as_uri(), [])
    if not any(edit.get("range", {}).get("start", {}) == math_mix_decl for edit in math_edits):
        raise AssertionError(f"math.mix rename missed declaration: {imported_function_rename!r}")
    if not any(edit.get("range", {}).get("start", {}) == main_math_mix for edit in main_edits):
        raise AssertionError(f"math.mix rename missed use site: {imported_function_rename!r}")
    if any(edit.get("range", {}).get("start", {}) == main_other_math_mix for edit in main_edits):
        raise AssertionError(f"math.mix rename edited other_math.mix: {imported_function_rename!r}")
    if (tmp / "other_math_utils.dd").as_uri() in rename_changes:
        raise AssertionError(f"math.mix rename edited unrelated module: {imported_function_rename!r}")

    imported_constant_refs = response(messages, 67)
    assert_nonempty(imported_constant_refs, "imported Dudu constant references")
    other_entities_source = (tmp / "other_entities.dd").read_text()
    entities_max_hp_decl = position(entities_source, "MAX_HP")
    other_entities_max_hp_decl = position(other_entities_source, "MAX_HP")
    main_max_hp = position(main_source, "Player(MAX_HP)", add=len("Player("))
    main_other_max_hp = position(main_source, "other_entities.MAX_HP", add=len("other_entities."))
    if not has_start(imported_constant_refs, entities.as_uri(), entities_max_hp_decl["line"], entities_max_hp_decl["character"]):
        raise AssertionError(f"missing MAX_HP declaration reference: {imported_constant_refs!r}")
    if not has_start(imported_constant_refs, main.as_uri(), main_max_hp["line"], main_max_hp["character"]):
        raise AssertionError(f"missing MAX_HP use reference: {imported_constant_refs!r}")
    if has_start(imported_constant_refs, (tmp / "other_entities.dd").as_uri(), other_entities_max_hp_decl["line"], other_entities_max_hp_decl["character"]):
        raise AssertionError(f"other_entities.MAX_HP declaration leaked into MAX_HP refs: {imported_constant_refs!r}")
    if has_start(imported_constant_refs, main.as_uri(), main_other_max_hp["line"], main_other_max_hp["character"]):
        raise AssertionError(f"other_entities.MAX_HP use leaked into MAX_HP refs: {imported_constant_refs!r}")
    imported_constant_rename = response(messages, 80)
    constant_rename_changes = imported_constant_rename.get("changes", {})
    entities_edits = constant_rename_changes.get(entities.as_uri(), [])
    main_constant_edits = constant_rename_changes.get(main.as_uri(), [])
    if not any(edit.get("range", {}).get("start", {}) == entities_max_hp_decl for edit in entities_edits):
        raise AssertionError(f"MAX_HP rename missed declaration: {imported_constant_rename!r}")
    if not any(edit.get("range", {}).get("start", {}) == main_max_hp for edit in main_constant_edits):
        raise AssertionError(f"MAX_HP rename missed use site: {imported_constant_rename!r}")
    if any(edit.get("range", {}).get("start", {}) == main_other_max_hp for edit in main_constant_edits):
        raise AssertionError(f"MAX_HP rename edited other_entities.MAX_HP: {imported_constant_rename!r}")
    if (tmp / "other_entities.dd").as_uri() in constant_rename_changes:
        raise AssertionError(f"MAX_HP rename edited unrelated module: {imported_constant_rename!r}")
    assert_completion_labels(response(messages, 33), ["MAGIC", "mix"])
    assert_documentation_contains(item_named(response(messages, 33), "mix"), "Mixes two numbers")
    assert_completion_labels(response(messages, 34), ["transitive_value"])
    member_completion = response(messages, 35)
    assert_completion_labels(member_completion, ["move", "hp"])
    assert_documentation_contains(item_named(member_completion, "move"), "Moves the player docs.")
    counter_member_completion = response(messages, 75)
    assert_completion_labels(counter_member_completion, ["LIMIT", "count", "bump"])
    assert_documentation_contains(item_named(counter_member_completion, "LIMIT"), "Counter limit docs.")
    assert_documentation_contains(item_named(counter_member_completion, "count"), "Counter mutable count docs.")
    assert_documentation_contains(item_named(counter_member_completion, "bump"), "Bumps the counter docs.")
    enum_member_completion = response(messages, 119)
    assert_completion_labels(enum_member_completion, ["Play", "Pause"])
    enum_play_item = item_named(enum_member_completion, "Play")
    if enum_play_item.get("kind") != 20:
        raise AssertionError(f"Mode.Play completion was not an enum member: {enum_play_item!r}")
    assert_documentation_contains(enum_play_item, "Mode play docs.")
    sum_variant_completion = response(messages, 120)
    assert_completion_labels(sum_variant_completion, ["Eof", "IntLit"])
    int_lit_item = item_named(sum_variant_completion, "IntLit")
    if int_lit_item.get("kind") != 20:
        raise AssertionError(f"Token.IntLit completion was not an enum member: {int_lit_item!r}")
    assert_documentation_contains(int_lit_item, "Integer token docs.")
    counter_signature_help = response(messages, 76)
    counter_signature_docs = counter_signature_help["signatures"][0]["documentation"]["value"]
    if "Bumps the counter docs." not in counter_signature_docs:
        raise AssertionError(f"missing Counter.bump signature docs: {counter_signature_help!r}")
    player_signature_help = response(messages, 77)
    player_signature = player_signature_help["signatures"][0]
    if "Player(hp: i32)" not in player_signature["label"]:
        raise AssertionError(f"missing Player constructor signature: {player_signature_help!r}")
    if "Runtime player docs." not in player_signature["documentation"]["value"]:
        raise AssertionError(f"missing Player constructor docs: {player_signature_help!r}")
    signature_help = response(messages, 36)
    signature_docs = signature_help["signatures"][0]["documentation"]["value"]
    if "Mixes two numbers for signature docs." not in signature_docs:
        raise AssertionError(f"missing signature docs: {signature_help!r}")
    module_hover = response(messages, 37)
    module_hover_value = module_hover["contents"]["value"]
    if "Entities module docs." not in module_hover_value:
        raise AssertionError(f"missing module docs: {module_hover!r}")
    player_type_hover = response(messages, 145)["contents"]["value"]
    if "class Player:" not in player_type_hover or "hp: i32" not in player_type_hover:
        raise AssertionError(f"missing Player definition hover: {player_type_hover!r}")
    if "size = 4 bytes, align = 4 bytes" not in player_type_hover:
        raise AssertionError(f"missing Player layout hover: {player_type_hover!r}")
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
    mode_play = position(entities_source, "    Play", add=4)
    other_play = position(entities_source, "    Play", occurrence=1, add=4)
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
    method_use_refs = response(messages, 43)
    assert_nonempty(method_use_refs, "method use identity references")
    main_player_move = position(main_source, "player.move", add=len("player."))
    player_move_decl = position(entities_source, "move(self")
    enemy_move_decl = position(entities_source, "move(self", occurrence=1)
    if not has_start(method_use_refs, main.as_uri(), main_player_move["line"], main_player_move["character"]):
        raise AssertionError(f"missing main player.move use reference: {method_use_refs!r}")
    if not has_start(method_use_refs, entities.as_uri(), player_move_decl["line"], player_move_decl["character"]):
        raise AssertionError(f"missing Player.move declaration through use-site refs: {method_use_refs!r}")
    if has_start(method_use_refs, entities.as_uri(), enemy_move_decl["line"], enemy_move_decl["character"]):
        raise AssertionError(f"Enemy.move leaked into Player.move refs: {method_use_refs!r}")
    method_decl_refs = response(messages, 44)
    assert_nonempty(method_decl_refs, "method declaration identity references")
    if not has_start(method_decl_refs, main.as_uri(), main_player_move["line"], main_player_move["character"]):
        raise AssertionError(f"missing main player.move through declaration refs: {method_decl_refs!r}")
    if not has_start(method_decl_refs, entities.as_uri(), player_move_decl["line"], player_move_decl["character"]):
        raise AssertionError(f"missing Player.move declaration reference: {method_decl_refs!r}")
    if has_start(method_decl_refs, entities.as_uri(), enemy_move_decl["line"], enemy_move_decl["character"]):
        raise AssertionError(f"Enemy.move leaked into declaration refs: {method_decl_refs!r}")
    member_hover = response(messages, 45)
    member_hover_value = member_hover["contents"]["value"]
    if "hp: i32" not in member_hover_value or "Current hit points docs." not in member_hover_value:
        raise AssertionError(f"missing member hover field docs: {member_hover!r}")
    enum_value_hover = response(messages, 47)
    enum_value_hover_value = enum_value_hover["contents"]["value"]
    if "enum variant Mode.Play" not in enum_value_hover_value or "Mode play docs." not in enum_value_hover_value:
        raise AssertionError(f"missing imported enum value hover docs: {enum_value_hover!r}")
    token_variant_hover = response(messages, 62)
    token_variant_hover_value = token_variant_hover["contents"]["value"]
    if "enum variant Token.IntLit" not in token_variant_hover_value or "Integer token docs." not in token_variant_hover_value:
        raise AssertionError(f"missing sum-type variant hover docs: {token_variant_hover!r}")
    token_variant_refs = response(messages, 63)
    token_int_lit_decl = position(entities_source, "    IntLit(i64)", add=4)
    other_token_int_lit_decl = position(entities_source, "    IntLit(i64)", occurrence=1, add=4)
    main_token_int_lit = position(main_source, "Token.IntLit", add=len("Token."))
    if not has_start(token_variant_refs, entities.as_uri(), token_int_lit_decl["line"], token_int_lit_decl["character"]):
        raise AssertionError(f"missing Token.IntLit declaration reference: {token_variant_refs!r}")
    if not has_start(token_variant_refs, main.as_uri(), main_token_int_lit["line"], main_token_int_lit["character"]):
        raise AssertionError(f"missing Token.IntLit use reference: {token_variant_refs!r}")
    if has_start(token_variant_refs, entities.as_uri(), other_token_int_lit_decl["line"], other_token_int_lit_decl["character"]):
        raise AssertionError(f"OtherToken.IntLit leaked into Token.IntLit refs: {token_variant_refs!r}")
    alias_hover = response(messages, 48)
    alias_hover_value = alias_hover["contents"]["value"]
    if "type PlayerId = i32" not in alias_hover_value or "Player id alias docs." not in alias_hover_value:
        raise AssertionError(f"missing imported alias hover docs: {alias_hover!r}")
    alias_definition = response(messages, 49)
    if alias_definition["uri"] != entities.as_uri():
        raise AssertionError(f"imported alias definition did not jump to source module: {alias_definition!r}")
    if alias_definition["range"]["start"]["line"] != 5:
        raise AssertionError(f"imported alias definition jumped to wrong line: {alias_definition!r}")
    counter_count_definition = response(messages, 68)
    counter_count_decl = position(entities_source, "count: static[i32]")
    other_counter_count_decl = position(entities_source, "count: static[i32]", occurrence=1)
    if counter_count_definition["uri"] != entities.as_uri():
        raise AssertionError(f"Counter.count definition did not jump to source module: {counter_count_definition!r}")
    if counter_count_definition["range"]["start"]["line"] != counter_count_decl["line"]:
        raise AssertionError(f"Counter.count definition jumped to wrong line: {counter_count_definition!r}")
    counter_limit_definition = response(messages, 69)
    counter_limit_decl = position(entities_source, "LIMIT: i32")
    if counter_limit_definition["uri"] != entities.as_uri():
        raise AssertionError(f"Counter.LIMIT definition did not jump to source module: {counter_limit_definition!r}")
    if counter_limit_definition["range"]["start"]["line"] != counter_limit_decl["line"]:
        raise AssertionError(f"Counter.LIMIT definition jumped to wrong line: {counter_limit_definition!r}")
    counter_count_hover = response(messages, 71)["contents"]["value"]
    if "count: i32" not in counter_count_hover or "Counter mutable count docs." not in counter_count_hover:
        raise AssertionError(f"missing Counter.count hover docs: {counter_count_hover!r}")
    counter_bump_hover = response(messages, 72)["contents"]["value"]
    if "bump() -> i32" not in counter_bump_hover or "Bumps the counter docs." not in counter_bump_hover:
        raise AssertionError(f"missing Counter.bump hover docs: {counter_bump_hover!r}")
    counter_count_refs = response(messages, 73)
    main_counter_count = position(main_source, "Counter.count", add=len("Counter."))
    entities_counter_count_use = position(entities_source, "Counter.count", add=len("Counter."))
    entities_other_counter_count_use = position(entities_source, "OtherCounter.count", add=len("OtherCounter."))
    if not has_start(counter_count_refs, entities.as_uri(), counter_count_decl["line"], counter_count_decl["character"]):
        raise AssertionError(f"missing Counter.count declaration ref: {counter_count_refs!r}")
    if not has_start(counter_count_refs, main.as_uri(), main_counter_count["line"], main_counter_count["character"]):
        raise AssertionError(f"missing Counter.count main ref: {counter_count_refs!r}")
    if not has_start(counter_count_refs, entities.as_uri(), entities_counter_count_use["line"], entities_counter_count_use["character"]):
        raise AssertionError(f"missing Counter.count method-body ref: {counter_count_refs!r}")
    if has_start(counter_count_refs, entities.as_uri(), other_counter_count_decl["line"], other_counter_count_decl["character"]):
        raise AssertionError(f"OtherCounter.count declaration leaked into Counter.count refs: {counter_count_refs!r}")
    if has_start(counter_count_refs, entities.as_uri(), entities_other_counter_count_use["line"], entities_other_counter_count_use["character"]):
        raise AssertionError(f"OtherCounter.count use leaked into Counter.count refs: {counter_count_refs!r}")
    counter_bump_refs = response(messages, 74)
    counter_bump_decl = position(entities_source, "bump()")
    other_counter_bump_decl = position(entities_source, "bump()", occurrence=1)
    main_counter_bump = position(main_source, "Counter.bump", add=len("Counter."))
    if not has_start(counter_bump_refs, entities.as_uri(), counter_bump_decl["line"], counter_bump_decl["character"]):
        raise AssertionError(f"missing Counter.bump declaration ref: {counter_bump_refs!r}")
    if not has_start(counter_bump_refs, main.as_uri(), main_counter_bump["line"], main_counter_bump["character"]):
        raise AssertionError(f"missing Counter.bump main ref: {counter_bump_refs!r}")
    if has_start(counter_bump_refs, entities.as_uri(), other_counter_bump_decl["line"], other_counter_bump_decl["character"]):
        raise AssertionError(f"OtherCounter.bump declaration leaked into Counter.bump refs: {counter_bump_refs!r}")
    initialize = response(messages, 1)
    rename_provider = initialize["capabilities"].get("renameProvider")
    if not isinstance(rename_provider, dict) or rename_provider.get("prepareProvider") is not True:
        raise AssertionError(f"server did not advertise prepareRename: {initialize!r}")
    if "codeLensProvider" in initialize["capabilities"]:
        raise AssertionError(f"server should not advertise reference CodeLens: {initialize!r}")
    prepare_math_mix = response(messages, 111)
    if prepare_math_mix.get("placeholder") != "mix":
        raise AssertionError(f"prepareRename did not return mix placeholder: {prepare_math_mix!r}")
    if prepare_math_mix.get("range", {}).get("start") != main_math_mix:
        raise AssertionError(f"prepareRename returned wrong mix range: {prepare_math_mix!r}")
    if response(messages, 112) is not None:
        raise AssertionError(f"prepareRename allowed native symbol rename: {response(messages, 112)!r}")
    semantic_legend = initialize["capabilities"]["semanticTokensProvider"]["legend"]
    legend = semantic_legend["tokenTypes"]
    token_modifiers = semantic_legend["tokenModifiers"]
    declaration = modifier_mask(token_modifiers, "declaration")
    readonly = modifier_mask(token_modifiers, "readonly")
    static = modifier_mask(token_modifiers, "static")
    native_modifier = modifier_mask(token_modifiers, "native")
    unresolved_modifier = modifier_mask(token_modifiers, "unresolved")
    decoded_tokens = decode_semantic_tokens(main_source, response(messages, 46)["data"], legend)
    if not has_semantic(decoded_tokens, "math", "namespace", 0):
        raise AssertionError(f"missing imported module namespace semantic token: {decoded_tokens!r}")
    if not has_semantic(decoded_tokens, "Player", "class", 0):
        raise AssertionError(f"missing imported class semantic token: {decoded_tokens!r}")
    if not has_semantic(decoded_tokens, "Box", "class", 0):
        raise AssertionError(f"missing imported generic class semantic token: {decoded_tokens!r}")
    if not has_semantic(decoded_tokens, "mix", "function", 0):
        raise AssertionError(f"missing imported function semantic token: {decoded_tokens!r}")
    if not has_semantic_type(decoded_tokens, "identity", "function"):
        raise AssertionError(f"missing imported generic function semantic token: {decoded_tokens!r}")
    if not has_semantic(decoded_tokens, "MAGIC", "variable", readonly):
        raise AssertionError(f"missing imported const semantic token: {decoded_tokens!r}")
    ops_tokens = decode_semantic_tokens(ops_source, response(messages, 142)["data"], legend)
    if not has_semantic(ops_tokens, "@operator", "macro", readonly):
        raise AssertionError(f"missing decorator semantic token: {ops_tokens!r}")
    native_tokens = decode_semantic_tokens(native_source, response(messages, 84)["data"], legend)
    if not has_semantic(native_tokens, "matrix_native_add", "function", native_modifier):
        raise AssertionError(f"missing native function semantic token: {native_tokens!r}")
    if not has_semantic(native_tokens, "DUDU_MATRIX_NATIVE_SCALE", "macro", native_modifier):
        raise AssertionError(f"missing native macro semantic token: {native_tokens!r}")
    if not has_semantic(native_tokens, "MATRIX_MODE_FAST", "variable", readonly | native_modifier):
        raise AssertionError(f"missing native value semantic token: {native_tokens!r}")
    if not has_semantic(native_tokens, "x", "property", native_modifier):
        raise AssertionError(f"missing native C field semantic token: {native_tokens!r}")
    entity_tokens = decode_semantic_tokens(entities_source, response(messages, 85)["data"], legend)
    if not has_semantic(entity_tokens, "count", "property", declaration | static):
        raise AssertionError(f"missing static field declaration semantic token: {entity_tokens!r}")
    if not has_semantic(entity_tokens, "LIMIT", "property", declaration | readonly):
        raise AssertionError(f"missing class constant declaration semantic token: {entity_tokens!r}")
    unresolved_tokens = decode_semantic_tokens(unresolved_source, response(messages, 70)["data"], legend)
    if not has_semantic(unresolved_tokens, "local_value", "variable", 0):
        raise AssertionError(f"known local was marked unresolved: {unresolved_tokens!r}")
    if not has_semantic(unresolved_tokens, "move", "method", 0):
        raise AssertionError(f"known method was marked unresolved: {unresolved_tokens!r}")
    if not has_semantic(unresolved_tokens, "missing_obj", "variable", unresolved_modifier):
        raise AssertionError(f"missing unresolved root variable token: {unresolved_tokens!r}")
    if not has_semantic(unresolved_tokens, "field", "property", unresolved_modifier):
        raise AssertionError(f"missing unresolved member property token: {unresolved_tokens!r}")
    if not has_semantic(unresolved_tokens, "missing_call", "function", unresolved_modifier):
        raise AssertionError(f"missing unresolved function token: {unresolved_tokens!r}")
    if not has_semantic(unresolved_tokens, "missing_value", "variable", unresolved_modifier):
        raise AssertionError(f"missing unresolved return variable token: {unresolved_tokens!r}")
    assert_symbol_names(response(messages, 40), ["Vec2", "main"])
    declaration_definition = response(messages, 131)
    if not isinstance(declaration_definition, list):
        raise AssertionError(f"declaration definition should return reference locations: {declaration_definition!r}")
    if not has_start(declaration_definition, main.as_uri(), main_math_mix["line"], main_math_mix["character"]):
        raise AssertionError(f"declaration definition missed main use: {declaration_definition!r}")
    if has_start(declaration_definition, (tmp / "math_utils.dd").as_uri(), math_mix_decl["line"], math_mix_decl["character"]):
        raise AssertionError(f"declaration definition should not include its own declaration: {declaration_definition!r}")
    assert_nonempty(response(messages, 41), "operator method definition")
    operator_decorator_hover = response(messages, 143)["contents"]["value"]
    if "@operator" not in operator_decorator_hover or "operator overload" not in operator_decorator_hover:
        raise AssertionError(f"missing @operator decorator hover: {operator_decorator_hover!r}")
    if response(messages, 144) is not None:
        raise AssertionError(f"built-in @operator decorator should not jump to compiler internals: {response(messages, 144)!r}")
    self_param = position(ops_source, "add(self", add=len("add("))
    other_param = position(ops_source, "other: Vec2", add=0)
    vec2_x_field = position(ops_source, "x: i32", add=0)
    self_root_definition = response(messages, 146)
    if self_root_definition["uri"] != ops.as_uri():
        raise AssertionError(f"self root definition did not jump to source file: {self_root_definition!r}")
    if self_root_definition["range"]["start"]["line"] != self_param["line"]:
        raise AssertionError(f"self root definition jumped to wrong line: {self_root_definition!r}")
    self_x_definition = response(messages, 147)
    if self_x_definition["uri"] != ops.as_uri():
        raise AssertionError(f"self.x member definition did not jump to source file: {self_x_definition!r}")
    if self_x_definition["range"]["start"]["line"] != vec2_x_field["line"]:
        raise AssertionError(f"self.x member definition jumped to wrong line: {self_x_definition!r}")
    other_root_definition = response(messages, 148)
    if other_root_definition["uri"] != ops.as_uri():
        raise AssertionError(f"other root definition did not jump to source file: {other_root_definition!r}")
    if other_root_definition["range"]["start"]["line"] != other_param["line"]:
        raise AssertionError(f"other root definition jumped to wrong line: {other_root_definition!r}")
    other_x_definition = response(messages, 149)
    if other_x_definition["uri"] != ops.as_uri():
        raise AssertionError(f"other.x member definition did not jump to source file: {other_x_definition!r}")
    if other_x_definition["range"]["start"]["line"] != vec2_x_field["line"]:
        raise AssertionError(f"other.x member definition jumped to wrong line: {other_x_definition!r}")
    self_root_hover = response(messages, 150)["contents"]["value"]
    if "self: Vec2" not in self_root_hover:
        raise AssertionError(f"self root hover did not show local type: {self_root_hover!r}")
    other_root_hover = response(messages, 151)["contents"]["value"]
    if "other: Vec2" not in other_root_hover:
        raise AssertionError(f"other root hover did not show local type: {other_root_hover!r}")
    loop_binding_hover = response(messages, 152)["contents"]["value"]
    if "i: i32" not in loop_binding_hover:
        raise AssertionError(f"for binding hover did not show inferred type: {loop_binding_hover!r}")
    range_hover = response(messages, 153)["contents"]["value"]
    if "range(stop: i32)" not in range_hover or "integer range iterable" not in range_hover:
        raise AssertionError(f"missing range builtin hover: {range_hover!r}")
    append_hover = response(messages, 154)["contents"]["value"]
    if "append(value: i32) -> void" not in append_hover or "push_back" not in append_hover:
        raise AssertionError(f"missing append builtin method hover: {append_hover!r}")
    if_hover = response(messages, 155)["contents"]["value"]
    if "keyword if" not in if_hover or "condition is true" not in if_hover:
        raise AssertionError(f"missing if keyword hover: {if_hover!r}")
    while_hover = response(messages, 156)["contents"]["value"]
    if "keyword while" not in while_hover or "condition stays true" not in while_hover:
        raise AssertionError(f"missing while keyword hover: {while_hover!r}")
    len_hover = response(messages, 157)["contents"]["value"]
    if "len(value) -> usize" not in len_hover or "Returns the length" not in len_hover:
        raise AssertionError(f"missing len builtin hover: {len_hover!r}")
    back_hover = response(messages, 158)["contents"]["value"]
    if "back() -> i32" not in back_hover or "Returns an element" not in back_hover:
        raise AssertionError(f"missing list back builtin method hover: {back_hover!r}")
    operator_use_definition = response(messages, 94)
    operator_add_decl = position(ops_source, "add(self", add=0)
    if operator_use_definition["uri"] != ops.as_uri():
        raise AssertionError(f"operator use definition did not jump to source file: {operator_use_definition!r}")
    if operator_use_definition["range"]["start"]["line"] != operator_add_decl["line"]:
        raise AssertionError(f"operator use definition jumped to wrong line: {operator_use_definition!r}")
    operator_hover = response(messages, 95)["contents"]["value"]
    if "add(self" not in operator_hover or "Adds two Vec2 values docs." not in operator_hover:
        raise AssertionError(f"missing operator use hover docs/signature: {operator_hover!r}")
    operator_refs = response(messages, 96)
    operator_use = position(ops_source, "left + right", add=len("left "))
    if not has_start(operator_refs, ops.as_uri(), operator_add_decl["line"], operator_add_decl["character"]):
        raise AssertionError(f"missing operator method declaration ref: {operator_refs!r}")
    if not has_start(operator_refs, ops.as_uri(), operator_use["line"], operator_use["character"]):
        raise AssertionError(f"missing operator use ref: {operator_refs!r}")
    generic_function_hover = response(messages, 97)["contents"]["value"]
    if "identity[T](value: T) -> T" not in generic_function_hover or "Returns a generic identity value docs." not in generic_function_hover:
        raise AssertionError(f"missing generic function hover docs/signature: {generic_function_hover!r}")
    generic_function_refs = response(messages, 98)
    identity_decl = position(entities_source, "identity[T]", add=0)
    identity_use = position(main_source, "identity[i32]", add=0)
    if not has_start(generic_function_refs, entities.as_uri(), identity_decl["line"], identity_decl["character"]):
        raise AssertionError(f"missing generic identity declaration ref: {generic_function_refs!r}")
    if not has_start(generic_function_refs, main.as_uri(), identity_use["line"], identity_use["character"]):
        raise AssertionError(f"missing generic identity use ref: {generic_function_refs!r}")
    generic_method_definition = response(messages, 99)
    box_echo_decl = position(entities_source, "echo[U]", add=0)
    if generic_method_definition["uri"] != entities.as_uri():
        raise AssertionError(f"generic method definition did not jump to source module: {generic_method_definition!r}")
    if generic_method_definition["range"]["start"]["line"] != box_echo_decl["line"]:
        raise AssertionError(f"generic method definition jumped to wrong line: {generic_method_definition!r}")
    generic_method_hover = response(messages, 100)["contents"]["value"]
    if "echo[U](self" not in generic_method_hover or "Echoes a generic method value docs." not in generic_method_hover:
        raise AssertionError(f"missing generic method hover docs/signature: {generic_method_hover!r}")
    generic_method_refs = response(messages, 101)
    box_echo_use = position(main_source, "box.echo", add=len("box."))
    if not has_start(generic_method_refs, entities.as_uri(), box_echo_decl["line"], box_echo_decl["character"]):
        raise AssertionError(f"missing generic method declaration ref: {generic_method_refs!r}")
    if not has_start(generic_method_refs, main.as_uri(), box_echo_use["line"], box_echo_use["character"]):
        raise AssertionError(f"missing generic method use ref: {generic_method_refs!r}")
    generic_class_hover = response(messages, 102)["contents"]["value"]
    if "class Box[T]" not in generic_class_hover or "Generic box docs." not in generic_class_hover:
        raise AssertionError(f"missing generic class hover docs/signature: {generic_class_hover!r}")
    primitive_hover = response(messages, 130)["contents"]["value"]
    if "type i32" not in primitive_hover or "C++ lowering: `std::int32_t`" not in primitive_hover:
        raise AssertionError(f"missing primitive type hover/lowering: {primitive_hover!r}")
    list_hover = response(messages, 132)["contents"]["value"]
    if "type list" not in list_hover or "std::vector<T>" not in list_hover:
        raise AssertionError(f"missing list type hover/lowering: {list_hover!r}")
    native_completion = response(messages, 50)
    assert_completion_labels(native_completion, ["matrix_native_add", "MatrixNativePoint", "DUDU_MATRIX_NATIVE_SCALE", "MATRIX_MODE_FAST"])
    assert_documentation_contains(item_named(native_completion, "matrix_native_add"), "Adds two matrix fixture integers.")
    assert_documentation_contains(item_named(native_completion, "DUDU_MATRIX_NATIVE_SCALE"), "Native scale macro docs.")
    assert_documentation_contains(item_named(native_completion, "MATRIX_MODE_FAST"), "Native mode fast docs.")
    for request_id in (51, 52, 53):
        assert_nonempty(response(messages, request_id), f"native request {request_id}")
    native_type_hover = response(messages, 51)["contents"]["value"]
    if "Native identity:" not in native_type_hover or "MatrixNativePoint" not in native_type_hover:
        raise AssertionError(f"missing native type identity: {native_type_hover!r}")
    native_header_definition = response(messages, 128)
    if native_header_definition["uri"] != (tmp / "native_bridge.h").as_uri():
        raise AssertionError(f"native header import did not jump to header file: {native_header_definition!r}")
    if native_header_definition["range"]["start"]["line"] != 0:
        raise AssertionError(f"native header import did not jump to header top: {native_header_definition!r}")
    native_alias_definition = response(messages, 129)
    if native_alias_definition["uri"] != (tmp / "native_bridge.h").as_uri():
        raise AssertionError(f"native import alias did not jump to header file: {native_alias_definition!r}")
    if native_alias_definition["range"]["start"]["line"] != 0:
        raise AssertionError(f"native import alias did not jump to header top: {native_alias_definition!r}")
    wrapped_native_type_definition = response(messages, 127)
    if not wrapped_native_type_definition["uri"].endswith("/native_bridge.h"):
        raise AssertionError(
            f"wrapped native type definition did not jump to header: {wrapped_native_type_definition!r}"
        )
    native_type_decl = position((tmp / "native_bridge.h").read_text(), "MatrixNativePoint {")
    if wrapped_native_type_definition["range"]["start"]["line"] != native_type_decl["line"]:
        raise AssertionError(
            f"wrapped native type definition jumped to wrong line: {wrapped_native_type_definition!r}"
        )
    native_macro_hover = response(messages, 53)["contents"]["value"]
    if "Native identity: `path:DUDU_MATRIX_NATIVE_SCALE`" not in native_macro_hover:
        raise AssertionError(f"missing native macro identity: {native_macro_hover!r}")
    if "Native scale macro docs." not in native_macro_hover:
        raise AssertionError(f"missing native macro docs: {native_macro_hover!r}")
    native_macro_definition = response(messages, 121)
    if not native_macro_definition["uri"].endswith("/native_bridge.h"):
        raise AssertionError(f"native macro definition did not jump to header: {native_macro_definition!r}")
    native_macro_decl = position((tmp / "native_bridge.h").read_text(), "DUDU_MATRIX_NATIVE_SCALE")
    if native_macro_definition["range"]["start"]["line"] != native_macro_decl["line"]:
        raise AssertionError(f"native macro definition jumped to wrong line: {native_macro_definition!r}")
    native_macro_refs = response(messages, 122)
    native_macro_use = position(native_source, "nb.DUDU_MATRIX_NATIVE_SCALE", add=len("nb."))
    if not has_start(native_macro_refs, native.as_uri(), native_macro_use["line"], native_macro_use["character"]):
        raise AssertionError(f"missing native macro reference: {native_macro_refs!r}")
    native_macro_signature_help = response(messages, 123)
    native_macro_signature_docs = native_macro_signature_help["signatures"][0]["documentation"]["value"]
    if "Native scale macro docs." not in native_macro_signature_docs:
        raise AssertionError(f"missing native macro signature docs: {native_macro_signature_help!r}")
    assert_symbol_names(response(messages, 124), ["main"])
    assert_nonempty(response(messages, 54), "native function references")
    native_value_hover = response(messages, 91)["contents"]["value"]
    if "MATRIX_MODE_FAST:" not in native_value_hover or "Native mode fast docs." not in native_value_hover:
        raise AssertionError(f"missing native value hover docs: {native_value_hover!r}")
    native_value_definition = response(messages, 92)
    if not native_value_definition["uri"].endswith("/native_bridge.h"):
        raise AssertionError(f"native value definition did not jump to header: {native_value_definition!r}")
    native_value_refs = response(messages, 93)
    native_value_use = position(native_source, "nb.MATRIX_MODE_FAST", add=len("nb."))
    if not has_start(native_value_refs, native.as_uri(), native_value_use["line"], native_value_use["character"]):
        raise AssertionError(f"missing native value reference: {native_value_refs!r}")
    native_signature_help = response(messages, 55)
    native_signature_docs = native_signature_help["signatures"][0]["documentation"]["value"]
    if "Adds two matrix fixture integers." not in native_signature_docs:
        raise AssertionError(f"missing native signature docs: {native_signature_help!r}")
    native_c_field_hover = response(messages, 116)["contents"]["value"]
    if (
        "x:" not in native_c_field_hover
        or "Native point x coordinate docs." not in native_c_field_hover
        or "Native identity:" not in native_c_field_hover
    ):
        raise AssertionError(f"missing native C field hover docs: {native_c_field_hover!r}")
    native_c_field_definition = response(messages, 117)
    if not native_c_field_definition["uri"].endswith("/native_bridge.h"):
        raise AssertionError(f"native C field definition did not jump to header: {native_c_field_definition!r}")
    native_c_field_decl = position((tmp / "native_bridge.h").read_text(), "    int x;", add=len("    int "))
    if native_c_field_definition["range"]["start"]["line"] != native_c_field_decl["line"]:
        raise AssertionError(f"native C field definition jumped to wrong line: {native_c_field_definition!r}")
    native_c_field_refs = response(messages, 118)
    native_c_field_assign = position(native_source, "point.x", add=len("point."))
    native_c_field_call = position(native_source, "point.x", occurrence=1, add=len("point."))
    if not has_start(native_c_field_refs, native.as_uri(), native_c_field_assign["line"], native_c_field_assign["character"]):
        raise AssertionError(f"missing native C field assignment ref: {native_c_field_refs!r}")
    if not has_start(native_c_field_refs, native.as_uri(), native_c_field_call["line"], native_c_field_call["character"]):
        raise AssertionError(f"missing native C field call ref: {native_c_field_refs!r}")
    native_context_field_definition = response(messages, 113)
    if not native_context_field_definition["uri"].endswith("/needs_c_context.h"):
        raise AssertionError(
            f"native context field definition did not jump to imported header: {native_context_field_definition!r}"
        )
    native_context_field_decl = position((tmp / "needs_c_context.h").read_text(), "count")
    if native_context_field_definition["range"]["start"]["line"] != native_context_field_decl["line"]:
        raise AssertionError(
            f"native context field definition jumped to wrong line: {native_context_field_definition!r}"
        )
    native_context_field_hover = response(messages, 114)["contents"]["value"]
    if "count:" not in native_context_field_hover:
        raise AssertionError(f"missing native context field hover: {native_context_field_hover!r}")
    native_context_field_refs = response(messages, 115)
    native_context_count_assign = position(native_context_source, "value.count", add=len("value."))
    native_context_count_return = position(native_context_source, "value.count", occurrence=1, add=len("value."))
    native_context_state_assign = position(native_context_source, "value.state", add=len("value."))
    if not has_start(
        native_context_field_refs,
        native_context.as_uri(),
        native_context_count_assign["line"],
        native_context_count_assign["character"],
    ):
        raise AssertionError(f"missing native context count assignment ref: {native_context_field_refs!r}")
    if not has_start(
        native_context_field_refs,
        native_context.as_uri(),
        native_context_count_return["line"],
        native_context_count_return["character"],
    ):
        raise AssertionError(f"missing native context count return ref: {native_context_field_refs!r}")
    if has_start(
        native_context_field_refs,
        native_context.as_uri(),
        native_context_state_assign["line"],
        native_context_state_assign["character"],
    ):
        raise AssertionError(f"native context state field leaked into count refs: {native_context_field_refs!r}")
    native_member_completion = response(messages, 56)
    assert_completion_labels(native_member_completion, ["scaled", "value"])
    assert_documentation_contains(item_named(native_member_completion, "scaled"), "Scales the matrix widget by a factor.")
    assert_documentation_contains(item_named(native_member_completion, "value"), "Current widget value.")
    native_member_hover = response(messages, 57)["contents"]["value"]
    if "Current widget value." not in native_member_hover:
        raise AssertionError(f"missing native member hover docs: {native_member_hover!r}")
    native_member_signature = response(messages, 58)
    native_member_signature_docs = native_member_signature["signatures"][0]["documentation"]["value"]
    if "Scales the matrix widget by a factor." not in native_member_signature_docs:
        raise AssertionError(f"missing native member signature docs: {native_member_signature!r}")
    native_member_definition = response(messages, 59)
    if not native_member_definition["uri"].endswith("/native_widget.hpp"):
        raise AssertionError(f"native member definition did not jump to header: {native_member_definition!r}")
    if native_member_definition["range"]["start"]["line"] != 14:
        raise AssertionError(f"native member definition jumped to wrong line: {native_member_definition!r}")
    native_member_refs = response(messages, 60)
    if not has_start(native_member_refs, native_cpp.as_uri(), 4, len("    widget.")):
        raise AssertionError(f"missing native member reference in source doc: {native_member_refs!r}")
    if not has_start(native_member_refs, native_cpp_same.as_uri(), 4, len("    return widget.scaled(3) + widget.")):
        raise AssertionError(f"missing native member reference in same-header doc: {native_member_refs!r}")
    if has_start(native_member_refs, native_cpp_other.as_uri(), 4, len("    return widget.scaled(4) + widget.")):
        raise AssertionError(f"unrelated native member reference leaked across receiver type: {native_member_refs!r}")
    native_class_hover = response(messages, 61)["contents"]["value"]
    if "native class MatrixWidget" not in native_class_hover or "Matrix widget class docs." not in native_class_hover:
        raise AssertionError(f"missing native class header docs: {native_class_hover!r}")
    native_method_definition = response(messages, 64)
    if not native_method_definition["uri"].endswith("/native_widget.hpp"):
        raise AssertionError(f"native method definition did not jump to header: {native_method_definition!r}")
    if native_method_definition["range"]["start"]["line"] != 9:
        raise AssertionError(f"native method definition jumped to wrong line: {native_method_definition!r}")
    native_method_refs = response(messages, 65)
    if not has_start(native_method_refs, native_cpp.as_uri(), 5, len("    return widget.")):
        raise AssertionError(f"missing native method reference in source doc: {native_method_refs!r}")
    if not has_start(native_method_refs, native_cpp_same.as_uri(), 4, len("    return widget.")):
        raise AssertionError(f"missing native method reference in same-header doc: {native_method_refs!r}")
    if has_start(native_method_refs, native_cpp_other.as_uri(), 4, len("    return widget.")):
        raise AssertionError(f"unrelated native method reference leaked across receiver type: {native_method_refs!r}")
    native_constructor_signature = response(messages, 81)
    native_constructor_label = native_constructor_signature["signatures"][0]["label"]
    native_constructor_docs = native_constructor_signature["signatures"][0]["documentation"]["value"]
    if "MatrixWidget(arg0: i32)" not in native_constructor_label:
        raise AssertionError(f"missing native constructor signature: {native_constructor_signature!r}")
    if "Builds a matrix widget from a seed." not in native_constructor_docs:
        raise AssertionError(f"missing native constructor docs: {native_constructor_signature!r}")
    native_constructor_definition = response(messages, 82)
    if not native_constructor_definition["uri"].endswith("/native_widget.hpp"):
        raise AssertionError(f"native constructor definition did not jump to header: {native_constructor_definition!r}")
    if native_constructor_definition["range"]["start"]["line"] != 6:
        raise AssertionError(f"native constructor definition jumped to wrong line: {native_constructor_definition!r}")
    native_constructor_hover = response(messages, 83)["contents"]["value"]
    if "MatrixWidget(arg0: i32)" not in native_constructor_hover:
        raise AssertionError(f"missing native constructor hover signature: {native_constructor_hover!r}")
    if "Builds a matrix widget from a seed." not in native_constructor_hover:
        raise AssertionError(f"missing native constructor hover docs: {native_constructor_hover!r}")
    native_namespace_hover = response(messages, 86)["contents"]["value"]
    if "native namespace matrix_space" not in native_namespace_hover:
        raise AssertionError(f"missing native namespace hover: {native_namespace_hover!r}")
    if "Native identity:" not in native_namespace_hover:
        raise AssertionError(f"missing native namespace identity: {native_namespace_hover!r}")
    if "Matrix namespace docs." not in native_namespace_hover:
        raise AssertionError(f"missing native namespace docs: {native_namespace_hover!r}")
    native_namespace_definition = response(messages, 87)
    if not native_namespace_definition["uri"].endswith("/native_namespace.hpp"):
        raise AssertionError(f"native namespace definition did not jump to header: {native_namespace_definition!r}")
    if native_namespace_definition["range"]["start"]["line"] != 3:
        raise AssertionError(f"native namespace definition jumped to wrong line: {native_namespace_definition!r}")
    native_namespace_completion = response(messages, 88)
    assert_completion_labels(native_namespace_completion, ["namespaced_add", "identity"])
    assert_documentation_contains(item_named(native_namespace_completion, "namespaced_add"), "Adds inside a native namespace.")
    assert_documentation_contains(item_named(native_namespace_completion, "identity"), "Returns a native template identity value.")
    native_namespace_tokens = decode_semantic_tokens(native_namespace_source, response(messages, 89)["data"], legend)
    if not has_semantic(native_namespace_tokens, "matrix_space", "namespace", native_modifier):
        raise AssertionError(f"missing native namespace semantic token: {native_namespace_tokens!r}")
    if not has_semantic(native_namespace_tokens, "identity", "function", native_modifier):
        raise AssertionError(f"missing native template function semantic token: {native_namespace_tokens!r}")
    native_namespace_refs = response(messages, 90)
    native_namespace_use = position(native_namespace_source, "matrix_space.namespaced_add")
    native_namespace_same_use = position(native_namespace_same_source, "matrix_space.namespaced_add")
    native_namespace_other_use = position(native_namespace_other_source, "matrix_space.namespaced_add")
    if not has_start(native_namespace_refs, native_namespace.as_uri(), native_namespace_use["line"], native_namespace_use["character"]):
        raise AssertionError(f"missing native namespace source reference: {native_namespace_refs!r}")
    if not has_start(native_namespace_refs, native_namespace_same.as_uri(), native_namespace_same_use["line"], native_namespace_same_use["character"]):
        raise AssertionError(f"missing same-header native namespace reference: {native_namespace_refs!r}")
    if has_start(native_namespace_refs, native_namespace_other.as_uri(), native_namespace_other_use["line"], native_namespace_other_use["character"]):
        raise AssertionError(f"unrelated native namespace reference leaked: {native_namespace_refs!r}")
    native_template_hover = response(messages, 103)["contents"]["value"]
    if "identity" not in native_template_hover or "Returns a native template identity value." not in native_template_hover:
        raise AssertionError(f"missing native template hover docs/signature: {native_template_hover!r}")
    native_template_definition = response(messages, 104)
    if not native_template_definition["uri"].endswith("/native_namespace.hpp"):
        raise AssertionError(f"native template definition did not jump to header: {native_template_definition!r}")
    native_namespace_header = (tmp / "native_namespace.hpp").read_text()
    native_template_decl = position(native_namespace_header, "template <typename T>", add=0)
    if native_template_definition["range"]["start"]["line"] != native_template_decl["line"]:
        raise AssertionError(f"native template definition jumped to wrong line: {native_template_definition!r}")
    native_template_refs = response(messages, 105)
    native_template_use = position(native_namespace_source, "matrix_space.identity", add=len("matrix_space."))
    if not has_start(native_template_refs, native_namespace.as_uri(), native_template_use["line"], native_template_use["character"]):
        raise AssertionError(f"missing native template function reference: {native_template_refs!r}")
    native_template_signature = response(messages, 106)
    if not native_template_signature.get("signatures"):
        raise AssertionError(f"missing native template signature help: {native_template_signature!r}")
    native_template_signature_docs = native_template_signature["signatures"][0]["documentation"]["value"]
    if "Returns a native template identity value." not in native_template_signature_docs:
        raise AssertionError(f"missing native template signature docs: {native_template_signature!r}")
    missing_diags = publish_diagnostics(messages, missing.as_uri())
    if not missing_diags or not missing_diags[-1]:
        raise AssertionError("missing import fixture did not publish diagnostics")
    missing_import_actions = response(messages, 107)
    missing_import_action = item_with_title(missing_import_actions, "Import MissingThing from available_symbol")
    missing_symbol_edits = missing_import_action.get("edit", {}).get("changes", {}).get(missing_symbol.as_uri(), [])
    expected_import = "from available_symbol import MissingThing\n"
    if not any(
        edit.get("newText") == expected_import
        and edit.get("range", {}).get("start", {}) == {"line": 0, "character": 0}
        for edit in missing_symbol_edits
    ):
        raise AssertionError(f"missing import quick fix edit: {missing_import_action!r}")
    organize_actions = response(messages, 108)
    organize_action = item_with_title(organize_actions, "Organize imports")
    organize_edits = organize_action.get("edit", {}).get("changes", {}).get(disorganized_imports.as_uri(), [])
    expected_organized = "import alpha\nimport zeta\n"
    if not any(
        edit.get("newText") == expected_organized
        and edit.get("range", {}).get("start", {}) == {"line": 0, "character": 0}
        and edit.get("range", {}).get("end", {}) == {"line": 2, "character": 0}
        for edit in organize_edits
    ):
        raise AssertionError(f"missing organize-imports edit: {organize_action!r}")
    lint_actions = response(messages, 109)
    lint_action = item_with_title(lint_actions, "Remove unused local")
    lint_edits = lint_action.get("edit", {}).get("changes", {}).get(lint_quickfix.as_uri(), [])
    if not any(
        edit.get("newText") == ""
        and edit.get("range", {}).get("start", {}) == {"line": 1, "character": 0}
        and edit.get("range", {}).get("end", {}) == {"line": 2, "character": 0}
        for edit in lint_edits
    ):
        raise AssertionError(f"missing lint quick fix edit: {lint_action!r}")
    resolved_completion = response(messages, 110)
    if resolved_completion.get("label") != "mix":
        raise AssertionError(f"completion resolve lost label: {resolved_completion!r}")
    if resolved_completion.get("detail") != "def mix(left: i32, right: i32) -> i32":
        raise AssertionError(f"completion resolve lost detail: {resolved_completion!r}")
    documentation = resolved_completion.get("documentation", {})
    if documentation.get("kind") != "markdown" or "Mixes two numbers" not in documentation.get("value", ""):
        raise AssertionError(f"completion resolve lost documentation: {resolved_completion!r}")

    print("lsp matrix checks passed")
finally:
    shutil.rmtree(tmp, ignore_errors=True)
PY
