from protocol import decode_semantic_tokens, has_semantic_type, has_start, position, response


def location_items(result):
    if result is None:
        return []
    return result if isinstance(result, list) else [result]


def assert_definition(result, path, source, needle, add=0, label="definition", occurrence=0):
    expected = position(source, needle, add=add, occurrence=occurrence)
    if not has_start(location_items(result), path.as_uri(), expected["line"], expected["character"]):
        raise AssertionError(f"{label} did not resolve to {path.name}:{expected}: {result!r}")


def assert_reference(result, path, source, needle, add=0, label="reference", occurrence=0):
    expected = position(source, needle, add=add, occurrence=occurrence)
    if not has_start(result or [], path.as_uri(), expected["line"], expected["character"]):
        raise AssertionError(f"{label} missing {path.name}:{expected}: {result!r}")


def assert_no_reference(result, path, source, needle, add=0, label="reference", occurrence=0):
    expected = position(source, needle, add=add, occurrence=occurrence)
    if has_start(result or [], path.as_uri(), expected["line"], expected["character"]):
        raise AssertionError(f"{label} leaked to {path.name}:{expected}: {result!r}")


def hover_value(messages, request_id):
    result = response(messages, request_id)
    if not result or not result.get("contents"):
        raise AssertionError(f"hover {request_id} returned no information: {result!r}")
    return result["contents"].get("value", "")


def edit_starts(edit_result, path):
    return {
        (edit["range"]["start"]["line"], edit["range"]["start"]["character"])
        for edit in (edit_result or {}).get("changes", {}).get(path.as_uri(), [])
    }


def assert_advanced_behavior(messages, workspace):
    model = workspace.model
    inheritance = workspace.inheritance
    main = workspace.main
    model_source = workspace.model_source
    inheritance_source = workspace.inheritance_source
    main_source = workspace.main_source

    assert_definition(response(messages, 10), main, main_source, "for item", len("for "), "loop binding")
    assert_definition(
        response(messages, 11),
        main,
        main_source,
        "Data(payload)",
        len("Data("),
        "match binding",
    )
    loop_refs = response(messages, 12)
    assert_reference(loop_refs, main, main_source, "for item", len("for "), "loop refs")
    assert_reference(loop_refs, main, main_source, "total += item", len("total += "), "loop refs")
    match_refs = response(messages, 13)
    assert_reference(match_refs, main, main_source, "Data(payload)", len("Data("), "match refs")
    assert_reference(match_refs, main, main_source, "total + payload", len("total + "), "match refs")
    if "item: i32" not in hover_value(messages, 14):
        raise AssertionError("loop binding hover did not preserve inferred element type")
    if "payload: i32" not in hover_value(messages, 15):
        raise AssertionError("match binding hover did not preserve payload field type")

    assert_definition(response(messages, 20), model, model_source, "class Buffer[T", len("class Buffer["), "class type parameter")
    assert_definition(response(messages, 21), model, model_source, "class Buffer[T, N", len("class Buffer[T, "), "class value parameter")
    type_refs = response(messages, 22)
    assert_reference(type_refs, model, model_source, "class Buffer[T", len("class Buffer["), "T refs")
    assert_reference(type_refs, model, model_source, "values: array[T]", len("values: array["), "T refs")
    value_refs = response(messages, 23)
    assert_reference(value_refs, model, model_source, "class Buffer[T, N", len("class Buffer[T, "), "N refs")
    assert_reference(value_refs, model, model_source, "values: array[T][N]", len("values: array[T]["), "N refs")
    assert_definition(response(messages, 24), model, model_source, "copy_values[T, N", len("copy_values[T, "), "function value parameter")
    if "N: usize" not in hover_value(messages, 25):
        raise AssertionError("value generic hover did not report usize")

    assert_definition(response(messages, 26), model, model_source, "def copy_values", len("def "), "re-exported function")
    assert_definition(response(messages, 27), model, model_source, "type FourInts", len("type "), "re-exported alias")
    assert_definition(response(messages, 28), model, model_source, "class Buffer", len("class "), "re-exported generic class")
    if "array[i32][4]" not in hover_value(messages, 29):
        raise AssertionError("generic result hover did not fold the inferred extent")
    if "array_view[i32][2]" not in hover_value(messages, 30):
        raise AssertionError("slice result hover did not preserve its result extent")
    signature = response(messages, 32)
    if not signature or "copy_values" not in signature["signatures"][0]["label"]:
        raise AssertionError(f"generic signature help was missing: {signature!r}")

    assert_definition(response(messages, 40), inheritance, inheritance_source, "def transform", len("def "), "super method")
    assert_definition(response(messages, 41), inheritance, inheritance_source, "def transform", len("def "), "derived override", occurrence=1)
    assert_definition(response(messages, 42), workspace.other, workspace.other_source, "def transform", len("def "), "same-spelling other method")
    assert_definition(response(messages, 43), inheritance, inheritance_source, "def read", len("def "), "concrete abstract override", occurrence=1)

    base_refs = response(messages, 44)
    assert_reference(base_refs, inheritance, inheritance_source, "def transform", len("def "), "base refs")
    assert_reference(base_refs, inheritance, inheritance_source, "super.transform", len("super."), "base refs")
    assert_no_reference(base_refs, main, main_source, "processor.transform", len("processor."), "base refs")
    derived_refs = response(messages, 45)
    assert_reference(derived_refs, inheritance, inheritance_source, "def transform", len("def "), "derived refs", occurrence=1)
    assert_reference(derived_refs, main, main_source, "processor.transform", len("processor."), "derived refs")
    assert_no_reference(derived_refs, main, main_source, "other_processor.transform", len("other_processor."), "derived refs")
    if "ScalingProcessor.transform" not in hover_value(messages, 46):
        raise AssertionError("override hover did not identify the concrete receiver method")
    completion_labels = {item.get("label") for item in response(messages, 47)}
    if not {"transform", "factor"}.issubset(completion_labels):
        raise AssertionError(f"derived member completion omitted inherited/own members: {completion_labels}")
    method_signature = response(messages, 48)
    if not method_signature or "transform" not in method_signature["signatures"][0]["label"]:
        raise AssertionError(f"override signature help was missing: {method_signature!r}")

    module_target = response(messages, 49)
    if (
        not module_target
        or module_target.get("uri") != model.as_uri()
        or module_target.get("range", {}).get("start") != {"line": 0, "character": 0}
    ):
        raise AssertionError(f"facade module segment did not resolve to model.dd: {module_target!r}")
    assert_definition(response(messages, 50), model, model_source, "class Buffer", len("class "), "facade imported name")

    prepare_override = response(messages, 70)
    if not prepare_override or prepare_override.get("placeholder") != "transform":
        raise AssertionError(f"override prepareRename was unavailable: {prepare_override!r}")

    loop_rename = response(messages, 71)
    loop_starts = edit_starts(loop_rename, main)
    expected_loop = {
        tuple(position(main_source, "for item", len("for ")).values()),
        tuple(position(main_source, "total += item", len("total += ")).values()),
    }
    if loop_starts != expected_loop:
        raise AssertionError(f"loop rename did not stay in its binding scope: {loop_rename!r}")

    match_rename = response(messages, 72)
    match_starts = edit_starts(match_rename, main)
    expected_match = {
        tuple(position(main_source, "Data(payload)", len("Data(")).values()),
        tuple(position(main_source, "total + payload", len("total + ")).values()),
    }
    if match_starts != expected_match:
        raise AssertionError(f"match rename did not stay in its case scope: {match_rename!r}")

    generic_rename = response(messages, 73)
    generic_starts = edit_starts(generic_rename, model)
    class_t = tuple(position(model_source, "class Buffer[T", len("class Buffer[")).values())
    class_value_t = tuple(position(model_source, "values: array[T]", len("values: array[")).values())
    class_return_t = tuple(position(model_source, "def first(self) -> T", len("def first(self) -> ")).values())
    function_t = tuple(position(model_source, "copy_values[T", len("copy_values[")).values())
    if not {class_t, class_value_t, class_return_t}.issubset(generic_starts) or function_t in generic_starts:
        raise AssertionError(f"generic rename crossed owner identity: {generic_rename!r}")

    method_rename = response(messages, 74)
    inheritance_starts = edit_starts(method_rename, inheritance)
    main_starts = edit_starts(method_rename, main)
    derived_decl = tuple(position(inheritance_source, "def transform", len("def "), occurrence=1).values())
    derived_use = tuple(position(main_source, "processor.transform", len("processor.")).values())
    base_decl = tuple(position(inheritance_source, "def transform", len("def ")).values())
    unrelated_use = tuple(position(main_source, "other_processor.transform", len("other_processor.")).values())
    if derived_decl not in inheritance_starts or base_decl in inheritance_starts:
        raise AssertionError(f"override rename crossed base identity: {method_rename!r}")
    if derived_use not in main_starts or unrelated_use in main_starts:
        raise AssertionError(f"override rename crossed receiver identity: {method_rename!r}")
    if workspace.other.as_uri() in (method_rename or {}).get("changes", {}):
        raise AssertionError(f"override rename edited unrelated class: {method_rename!r}")

    initialize = response(messages, 1)
    legend = initialize["capabilities"]["semanticTokensProvider"]["legend"]["tokenTypes"]
    main_tokens = decode_semantic_tokens(main_source, response(messages, 60)["data"], legend)
    model_tokens = decode_semantic_tokens(model_source, response(messages, 61)["data"], legend)
    if not has_semantic_type(main_tokens, "item", "variable"):
        raise AssertionError("loop binding lacked a semantic variable token")
    if not has_semantic_type(main_tokens, "payload", "variable"):
        raise AssertionError("match binding lacked a semantic variable token")
    if not has_semantic_type(model_tokens, "T", "type"):
        raise AssertionError("generic type parameter lacked a semantic type token")
    if not has_semantic_type(model_tokens, "N", "variable"):
        raise AssertionError("generic value parameter lacked a semantic variable token")

    hints = response(messages, 62)
    hint_text = " ".join(
        part.get("value", "") if isinstance(part, dict) else str(part)
        for hint in hints
        for part in (hint.get("label") if isinstance(hint.get("label"), list) else [hint.get("label", "")])
    )
    compact_hint_text = "".join(hint_text.split())
    if "array[i32][4]" not in compact_hint_text or "array_view[i32][2]" not in compact_hint_text:
        raise AssertionError(f"advanced inferred shape hints were incomplete: {hint_text}")

    symbol_names = {item.get("name") for item in response(messages, 63)}
    if not {"Processor", "ScalingProcessor", "Source", "ConstantSource"}.issubset(symbol_names):
        raise AssertionError(f"inheritance document symbols were incomplete: {symbol_names}")
