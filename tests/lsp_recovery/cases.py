from dataclasses import dataclass


@dataclass(frozen=True)
class RecoveryCase:
    name: str
    damage: str
    diagnostic_source: str


CASES = [
    RecoveryCase("unfinished_call", "def damaged() -> i32:\n    broken = usable(\n    return 0", "dudu/parser"),
    RecoveryCase(
        "unfinished_generic",
        "def damaged() -> i32:\n    values: list[i32\n    return 0",
        "dudu/parser",
    ),
    RecoveryCase(
        "unfinished_index",
        "def damaged() -> i32:\n    values: list[i32] = []\n    broken = values[\n    return 0",
        "dudu/parser",
    ),
    RecoveryCase(
        "incomplete_member", "def damaged(player: Player) -> i32:\n    player.\n    return 0", "dudu/parser"
    ),
    RecoveryCase("missing_operand", "def damaged() -> i32:\n    broken = 1 +\n    return 0", "dudu/parser"),
    RecoveryCase(
        "unterminated_string",
        'def damaged() -> i32:\n    broken = "unfinished\n    return 0',
        "dudu/parser",
    ),
    RecoveryCase("missing_colon", "def damaged() -> i32:\n    if True\n        return 1\n    return 0", "dudu/parser"),
    RecoveryCase(
        "bad_loop",
        "def damaged() -> i32:\n    for value in range(3)\n        return value\n    return 0",
        "dudu/parser",
    ),
    RecoveryCase("bad_indent", "def damaged() -> i32:\n    before = 1\n  broken = 2\n    return before", "dudu/parser"),
    RecoveryCase("bad_return_type", "def damaged() ->:\n    return 0", "dudu/parser"),
    RecoveryCase("bad_class_member", "class Damaged:\n    valid: i32\n    broken i32", "dudu/parser"),
    RecoveryCase("bad_enum_payload", "enum Damaged:\n    Value:\n        broken i32", "dudu/parser"),
    RecoveryCase("bad_decorator", "@operator(\ndef damaged() -> i32:\n    return 0", "dudu/parser"),
    RecoveryCase(
        "bad_macro_attribute",
        "@macro(attributes=)\ndef damaged() -> i32:\n    return 0",
        "dudu/parser",
    ),
    RecoveryCase(
        "bad_match", "def damaged(value: i32) -> i32:\n    match value:\n        case:\n            return 1\n    return 0", "dudu/parser"
    ),
    RecoveryCase("bad_except", "def damaged() -> i32:\n    try:\n        return 1\n    except Error\n        return 0", "dudu/parser"),
    RecoveryCase("incomplete_import", "from missing_helper import", "dudu/parser"),
    RecoveryCase(
        "missing_native_header",
        "from cpp.path import ./dudu_recovery_missing_header.hpp\n\ndef damaged() -> i32:\n    return 0",
        "dudu/native-header",
    ),
    RecoveryCase("unknown_name", "def damaged() -> i32:\n    return name_that_does_not_exist", "dudu/sema"),
    RecoveryCase("unknown_type", "def damaged(value: MissingType) -> i32:\n    return 0", "dudu/sema"),
    RecoveryCase(
        "duplicate_function",
        "def duplicate() -> i32:\n    return 1\n\ndef duplicate() -> i32:\n    return 2",
        "dudu/sema",
    ),
]


def source_for(case):
    marker = f"current_{case.name}"
    return f"""class Player:
    hp: i32

def usable(value: i32) -> i32:
    return value + 1

{case.damage}

def {marker}(value: i32) -> i32:
    return usable(value)

def main() -> i32:
    result = {marker}(1)
    return result
"""
