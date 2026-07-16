from protocol import (
    assert_completion_labels,
    assert_documentation_contains,
    assert_nonempty,
    assert_symbol_names,
    decode_semantic_tokens,
    has_semantic,
    has_semantic_type,
    has_start,
    item_named,
    modifier_mask,
    position,
    response,
)


def assert_core_behavior(messages, workspace):
    tmp = workspace.tmp
    main = workspace.main
    entities = workspace.entities
    ops = workspace.ops
    native = workspace.native
    unresolved = workspace.unresolved
    containers = workspace.containers

    main_source = workspace.main_source
    entities_source = workspace.entities_source
    ops_source = workspace.ops_source
    native_source = workspace.native_source
    unresolved_source = workspace.unresolved_source
    containers_source = workspace.containers_source

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
    token_int_lit_decl = position(entities_source, "    IntLit:", add=4)
    other_token_int_lit_decl = position(entities_source, "    IntLit:", occurrence=1, add=4)
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
    if "self: &Vec2" not in self_root_hover:
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
