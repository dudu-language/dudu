from types import SimpleNamespace

from protocol import position


def create_workspace(tmp):
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
    IntLit:
        value: i64

enum OtherToken:
    IntLit:
        value: i64

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
    native_source = """from c.path import native_bridge.h as nb

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
    native_context_source = """from c.path import ./needs_c_context.h as native

def main() -> i32:
    value: native.DuduNeedsContext
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
    native_cpp_source = """from cpp.path import native_widget.hpp

def main() -> i32:
    widget: MatrixWidget = MatrixWidget(5)
    widget.value = 6
    return widget.scaled(2)
"""
    (tmp / "native_cpp_user.dd").write_text(native_cpp_source)
    native_cpp_same_source = """from cpp.path import native_widget.hpp

def same() -> i32:
    widget: MatrixWidget
    return widget.scaled(3) + widget.value
"""
    (tmp / "native_cpp_same.dd").write_text(native_cpp_same_source)
    native_cpp_other_source = """from cpp.path import native_other_widget.hpp

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
    native_namespace_source = """from cpp.path import native_namespace.hpp

def main() -> i32:
    return matrix_space.namespaced_add(2, 3) + matrix_space.identity[i32](4)
"""
    (tmp / "native_namespace_user.dd").write_text(native_namespace_source)
    native_namespace_same_source = """from cpp.path import native_namespace.hpp

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
    native_namespace_other_source = """from cpp.path import native_namespace_other.hpp

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

    return SimpleNamespace(**locals())
