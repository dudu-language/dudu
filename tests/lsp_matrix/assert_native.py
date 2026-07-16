from protocol import (
    assert_completion_labels,
    assert_documentation_contains,
    assert_nonempty,
    assert_symbol_names,
    decode_semantic_tokens,
    has_semantic,
    has_start,
    item_named,
    modifier_mask,
    position,
    publish_diagnostics,
    response,
)


def assert_native_behavior(messages, workspace):
    tmp = workspace.tmp
    native = workspace.native
    native_context = workspace.native_context
    native_cpp = workspace.native_cpp
    native_cpp_same = workspace.native_cpp_same
    native_cpp_other = workspace.native_cpp_other
    native_namespace = workspace.native_namespace
    native_namespace_same = workspace.native_namespace_same
    native_namespace_other = workspace.native_namespace_other

    native_source = workspace.native_source
    native_context_source = workspace.native_context_source
    native_cpp_source = workspace.native_cpp_source
    native_namespace_source = workspace.native_namespace_source
    native_namespace_same_source = workspace.native_namespace_same_source
    native_namespace_other_source = workspace.native_namespace_other_source

    initialize = response(messages, 1)
    semantic_legend = initialize["capabilities"]["semanticTokensProvider"]["legend"]
    legend = semantic_legend["tokenTypes"]
    token_modifiers = semantic_legend["tokenModifiers"]
    native_modifier = modifier_mask(token_modifiers, "native")

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
    if native_context_field_definition is None:
        raise AssertionError(
            "native context field definition was unresolved; diagnostics: "
            f"{publish_diagnostics(messages, native_context.as_uri())!r}"
        )
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
    if "MatrixWidget(seed: i32)" not in native_constructor_label:
        raise AssertionError(f"missing native constructor signature: {native_constructor_signature!r}")
    if "Builds a matrix widget from a seed." not in native_constructor_docs:
        raise AssertionError(f"missing native constructor docs: {native_constructor_signature!r}")
    native_constructor_definition = response(messages, 82)
    if not native_constructor_definition["uri"].endswith("/native_widget.hpp"):
        raise AssertionError(f"native constructor definition did not jump to header: {native_constructor_definition!r}")
    if native_constructor_definition["range"]["start"]["line"] != 6:
        raise AssertionError(f"native constructor definition jumped to wrong line: {native_constructor_definition!r}")
    native_constructor_hover = response(messages, 83)["contents"]["value"]
    if "MatrixWidget(seed: i32)" not in native_constructor_hover:
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
    if not has_start(native_namespace_refs, native_namespace_other.as_uri(), native_namespace_other_use["line"], native_namespace_other_use["character"]):
        raise AssertionError(f"missing reopened native namespace reference: {native_namespace_refs!r}")
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
