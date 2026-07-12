#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

expect_fail bad_duplicate --check "duplicate declaration: Vec"
expect_fail bad_type_alias --check "unknown type alias target: MissingThing"
expect_fail bad_parse_missing_colon --check "bad_parse_missing_colon.dd:1:17: expected : after function header"
expect_fail bad_indent_width --check "bad_indent_width.dd:2:1: indentation must be a multiple of four spaces"
expect_fail bad_dict_literal --emit-cpp "cannot assign set"
expect_fail bad_list_literal_type --emit-cpp "cannot assign list to list\\[i32\\]"
expect_fail bad_set_literal_type --emit-cpp "cannot assign set to set\\[str\\]"
expect_fail bad_dict_literal_key_type --emit-cpp "cannot assign dict to dict\\[str, i32\\]"
expect_fail bad_dict_literal_value_type --emit-cpp "cannot assign dict to dict\\[str, i32\\]"
expect_fail bad_enum_underlying --check "unknown enum underlying type: MissingType"
expect_fail bad_enum_duplicate --check "duplicate enum value: Value"
expect_fail bad_enum_value_name --check "enum values must be PascalCase: bad_value"
expect_fail bad_enum_match_missing --check "non-exhaustive match on Direction; missing cases: Direction.South"
expect_fail bad_enum_match_duplicate --check "unreachable duplicate case: Direction.North"
expect_fail bad_enum_match_guard_after_covered --check "unreachable duplicate case: Direction.North"
expect_fail bad_enum_match_unreachable_wildcard --check "unreachable wildcard case after exhaustive cases"
expect_fail bad_enum_match_after_wildcard --check "unreachable case after wildcard"
expect_fail bad_enum_match_unknown --check "unknown enum variant in pattern: Direction.East"
expect_fail bad_enum_match_guard --check "non-exhaustive match on Direction; missing cases: Direction.North"
expect_fail bad_payload_enum_field_name --check "enum payload field names must be snake_case: BadField"
expect_fail bad_payload_enum_field_type --check "unknown enum payload field type: MissingType"
expect_fail bad_payload_enum_duplicate_field --check "duplicate enum payload field: Message.Move.value"
expect_fail bad_payload_enum_case_arity --check "case Move expects 2 bindings, got 1"
expect_fail bad_payload_enum_named_case_field --check "unknown enum payload field in pattern: Move.z"
expect_fail bad_payload_enum_named_case_duplicate --check "duplicate enum payload field in pattern: Move.x"
expect_fail bad_payload_enum_guard_type --check "match guard must be bool, got i32"
expect_fail bad_payload_enum_guard_after_covered --check "unreachable duplicate case: Message.Move"
expect_fail bad_option_match_missing --check "non-exhaustive match on wrapper; missing cases: None"
expect_fail bad_option_match_unreachable_wildcard --check "unreachable wildcard case after exhaustive cases"
expect_fail bad_option_match_after_wildcard --check "unreachable case after wildcard"
expect_fail bad_option_match_guard_after_covered --check "unreachable duplicate case: Some"
expect_fail bad_result_match_case --check "case pattern must be Ok(...), Err(...), or _"
expect_fail bad_anonymous_variant_value --emit-cpp "cannot assign bool to variant\\[i32, str\\] without an explicit cast"
expect_fail bad_return --emit-cpp "return type mismatch: expected i32, got bool"
expect_fail bad_return --check "return type mismatch: expected i32, got bool"
expect_fail bad_return_implicit_cast --emit-cpp "return type mismatch: expected i64, got i32"
expect_fail bad_unknown_identifier --emit-cpp "unknown identifier: missing_value"
expect_fail bad_unknown_identifier --check "unknown identifier: missing_value"
expect_fail bad_unknown_function --emit-cpp "unknown function: missing"
expect_fail bad_unknown_dotted_call --emit-cpp "unknown function: missing.namespace"
expect_fail bad_unknown_dotted_template_call --emit-cpp "unknown function: missing.namespace\\[i32\\]"
expect_fail bad_unknown_base --check "unknown base class: MissingBase"
expect_fail bad_duplicate_base --check "duplicate base class: Entity"
expect_fail bad_override_missing --check "@override method has no matching base method: Player.draw"
expect_fail bad_override_signature --check "@override signature does not match base method: Square.area"
expect_fail bad_override_nonvirtual --check "@override target must be @virtual or @abstract: Square.area"
expect_fail bad_abstract_body --check "@abstract methods cannot have a body"
expect_fail bad_bodyless_method --check "bodyless method requires @abstract: area"
expect_fail bad_abstract_construct --emit-cpp "cannot construct abstract class: Shape; missing area() -> i32"
expect_fail bad_inherited_abstract_construct --emit-cpp "cannot construct abstract class: NamedShape; missing area() -> i32"
expect_fail bad_abstract_new --emit-cpp "cannot allocate abstract class: Shape"
expect_fail bad_super_no_base --check "super access requires a base class"
expect_fail bad_super_outside_method --check "super access outside class method"
expect_fail bad_super_multiple_bases --check "super access is ambiguous with multiple base classes"
expect_fail bad_super_init_order --emit-cpp "super.init must be the first statement in init"
expect_fail bad_super_init_outside_init --emit-cpp "super.init must be the first statement in init"
expect_fail bad_super_init_type --emit-cpp "constructor Entity argument 1 expects i32, got bool"
expect_fail bad_multiple_storage_bases --check "multiple inheritance allows at most one storage-bearing Dudu base"
expect_fail bad_multiple_noninterface_base --check "multiple inheritance non-storage bases must be abstract interface-like classes"
expect_fail bad_duplicate_inherited_field --check "multiple inheritance allows at most one storage-bearing Dudu base"
expect_fail bad_ambiguous_inherited_method --check "ambiguous inherited concrete method: label() -> str"
expect_fail bad_variadic_macro_arity --emit-cpp "no native overload of wrap.DUDU_WRAP_COUNT accepts 1 arguments"
expect_fail bad_native_overload_type --emit-cpp "candidate: dudu_native.overloaded(i32) -> i32"
expect_fail bad_native_overload_type --emit-cpp "reason: parameter 1 expects i32, got bool"
expect_fail bad_native_overload_multi_reason --emit-cpp "candidate: dudu_native.overloaded_pair(i32, f32) -> i32"
expect_fail bad_native_overload_multi_reason --emit-cpp "reason: parameter 1 expects i32, got bool"
expect_fail bad_native_overload_multi_reason --emit-cpp "reason: parameter 2 expects f32, got str"
expect_fail bad_native_overload_multi_reason --emit-cpp "candidate: dudu_native.overloaded_pair(f32, i32) -> i32"
expect_fail bad_native_overload_multi_reason --emit-cpp "reason: parameter 1 expects f32, got bool"
expect_fail bad_native_overload_multi_reason --emit-cpp "reason: parameter 2 expects i32, got str"
expect_fail bad_native_template_function --emit-cpp "candidate: dudu_template.identity(i32) -> i32"
expect_fail bad_native_template_function --emit-cpp "reason: parameter 1 expects i32, got bool"
expect_fail bad_native_alias_template_arity --emit-cpp "type depmeta.AliasCarrier expects 2 type arguments, got 1"
expect_fail bad_unknown_method --emit-cpp "unknown method: Counter.add"
expect_fail bad_duplicate_method_overload --check "duplicate method overload: Tensor.to"
expect_fail bad_out_of_line_method_class --emit-cpp "out-of-line method receiver is not a known class: Missing"
expect_fail bad_method_expr_receiver --emit-cpp "unknown method: Counter.missing"
expect_fail bad_method_arity --emit-cpp "function counter.add expects 1 arguments, got 0"
expect_fail bad_method_type --emit-cpp "argument 1 for counter.add expects i32, got bool"
expect_fail bad_init_return --emit-cpp "init cannot declare a return type"
expect_fail bad_del_param --emit-cpp "drop cannot take parameters"
expect_fail bad_init_arg_type --emit-cpp "constructor Counter argument 1 expects bool, got i32"
expect_fail bad_from_import_missing --emit-cpp "module 'bad_from_import_helper' has no symbol 'missing'"
expect_fail bad_from_import_collision_local --emit-cpp "from import name 'Thing' collides with an existing declaration; use 'as' to choose a unique local name"
expect_fail bad_from_import_collision_imports --emit-cpp "import name 'Thing' collides with an earlier direct import; use 'as' to choose a unique local name"
expect_fail bad_cycle_a --emit-cpp "cyclic module import: bad_cycle_a -> bad_cycle_b -> bad_cycle_a"
expect_fail bad_unknown_type --emit-cpp "unknown local type: MissingType"
expect_fail bad_nested_type --check "unknown parameter type: MissingType"
expect_fail bad_nested_local_type --emit-cpp "unknown local type: MissingType"
expect_fail bad_field_read --emit-cpp "unknown field: counter.missing"
expect_fail bad_member_expr_receiver --emit-cpp "unknown field: Point.missing"
expect_fail bad_const_ref_field --emit-cpp "unknown field: coin.missing"
expect_fail bad_tuple_destructure --emit-cpp "tuple destructuring count mismatch"
expect_fail bad_tuple_target --emit-cpp "tuple destructuring targets must be names"
expect_fail bad_tuple_duplicate_binding --emit-cpp "duplicate destructuring binding: value"
expect_fail bad_tuple_shadow_binding --emit-cpp "destructuring binding shadows local: value"
expect_fail bad_nested_field --emit-cpp "unknown field: outer.inner.missing"
expect_fail bad_naming --check "type names must be PascalCase: bad_type"
expect_fail bad_parameter_name --check "parameter names must be snake_case: Value"
expect_fail bad_local_name --emit-cpp "local names must be snake_case or ALL_CAPS: BadValue"
expect_fail bad_static_field_name --check "static field names must be snake_case: badName"
expect_fail bad_for_name --emit-cpp "local names must be snake_case or ALL_CAPS: BadValue"
expect_fail bad_missing_return --emit-cpp "missing return in function: bad"
expect_fail bad_constructor_field --emit-cpp "unknown constructor field: Point.z"
expect_fail bad_constructor_duplicate --emit-cpp "duplicate constructor field: x"
expect_fail bad_constructor_type --emit-cpp "constructor field Point.x expects i32, got bool"
expect_fail bad_constructor_positional_type --emit-cpp "constructor Point argument 1 expects i32, got bool"
expect_fail bad_result_ok_type --emit-cpp "return type mismatch: expected Result\\[i32, i32\\], got Ok\\[bool\\]"
expect_fail bad_result_err_type --emit-cpp "return type mismatch: expected Result\\[i32, i32\\], got Err\\[bool\\]"
expect_fail bad_ok_arity --emit-cpp "Ok expects 1 argument, got 0"; expect_fail bad_err_arity --emit-cpp "Err expects 1 argument, got 2"
expect_fail bad_new_assignment --emit-cpp "cannot assign \\*Node to \\*i32 without an explicit cast"; expect_fail bad_malloc_arity --emit-cpp "malloc expects 1 count argument, got 0"; expect_fail bad_alloc_type --emit-cpp "unknown allocation type: MissingType"; expect_fail bad_alloc_nested_type --emit-cpp "unknown allocation type: MissingType"
expect_fail bad_move_arity --emit-cpp "move expects 1 arguments, got 2"
expect_fail bad_sizeof_nested_type --emit-cpp "unknown sizeof type: MissingType"; expect_fail bad_offsetof_nested_type --emit-cpp "unknown offsetof type: MissingType"
expect_fail bad_offsetof_field_expr --emit-cpp "offsetof field argument must be a field name"
expect_fail bad_pointer_cast_nested_type --emit-cpp "unknown pointer cast type: MissingType"
expect_fail bad_delete_arity --emit-cpp "delete expects 1 pointer argument, got 2"; expect_fail bad_free_type --emit-cpp "free expects pointer, got i32"; expect_fail bad_bare_delete_type --emit-cpp "delete expects pointer, got i32"
expect_fail bad_void_return --emit-cpp "void function cannot return i32"; expect_fail bad_break_outside_loop --emit-cpp "break outside loop"
expect_fail bad_bare_return_value --emit-cpp "return type mismatch: expected i32, got void"
expect_fail bad_continue_outside_loop --emit-cpp "continue outside loop"
expect_fail bad_index_read_type --emit-cpp "cannot assign i32 to bool without an explicit cast"
expect_fail bad_empty_index --emit-cpp "index expression expects receiver and index"
expect_fail bad_index_non_container --emit-cpp "cannot index non-container: value"
expect_fail bad_array_empty_inference --emit-cpp "array shape cannot be inferred from an empty literal"
expect_fail bad_array_ragged_inference --emit-cpp "ragged array literal"
expect_fail bad_array_shape_mismatch --emit-cpp "array literal shape mismatch: expected \\[2, 2\\], got \\[3, 2\\]"
expect_fail bad_array_empty_shape_mismatch --emit-cpp "array literal shape mismatch: expected \\[2\\], got \\[0\\]"
expect_fail bad_array_element_type --emit-cpp "array literal element expects i32, got bool"
expect_fail bad_array_row_to_scalar --emit-cpp "cannot assign array\\[i32\\]\\[2\\] to i32 without an explicit cast"
expect_fail bad_array_too_many_indices --emit-cpp "too many indices for array: matrix"
expect_fail bad_array_matrix_slice --emit-cpp "cannot assign array_view\\[i32\\]\\[1, 2\\] to span\\[i32\\] without an explicit cast"
expect_fail bad_array_general_matrix_slice --emit-cpp "cannot assign array_view\\[i32\\]\\[1\\] to strided_span\\[i32\\] without an explicit cast"
expect_fail bad_swizzle_width --emit-cpp "unknown field: value.xyx"
expect_fail bad_swizzle_mixed_sets --emit-cpp "unknown field: color.rgxy"
expect_fail bad_swizzle_stpq_mixed_sets --emit-cpp "unknown field: coord.stxy"
expect_fail bad_swizzle_assignment_repeat --emit-cpp "swizzle assignment cannot repeat component: xx"
expect_fail bad_tensor_index_type --emit-cpp "no matching @operator(\"\\[\\]\") for indexed access to tensor"
expect_fail bad_tensor_index_set_index_type --emit-cpp "candidate Tensor2.set_at(i32, i32) -> void rejected: argument 1 expects i32, got bool"
expect_fail bad_tensor_index_set_member_type --emit-cpp "candidate Tensor2.set_at(i32, i32) -> void rejected: argument 1 expects i32, got bool"
expect_fail bad_tensor_index_set_value_type --emit-cpp "candidate Tensor2.set_at(i32, i32) -> void rejected: argument 2 expects i32, got bool"
expect_fail bad_tensor_index_set_return --check "indexed assignment operator methods must return void"
expect_fail bad_pairwise_indexer_missing_hook --emit-cpp "no matching @operator(\"\\[\\]\") for indexed access to tensor.pairwise"
expect_fail bad_cartesian_indexer_missing_hook --emit-cpp "no matching @operator(\"\\[\\]\") for indexed access to tensor.cartesian"
expect_fail bad_tensor_shape_dyn_to_concrete --emit-cpp "argument 1 for expect_static_shape expects Tensor\\[f32\\]\\[2, 2\\], got Tensor\\[f32\\]\\[dyn, 2\\]"
expect_fail bad_tensor_shape_generic_mismatch --emit-cpp "conflicting inferred type argument Inner: 3 vs 4 for matmul_shape"
expect_fail bad_tensor_shape_composition_mismatch --emit-cpp "conflicting inferred type argument Cols: 4 vs 5 for add_same_shape"
expect_fail bad_tensor_mask_compound_scatter --emit-cpp "compound indexed assignment requires @operator(\"\\[\\]=\") accepting the indexed value type"
expect_fail bad_assume_shape_unshaped --emit-cpp "assume_shape target must include shape metadata"
expect_fail bad_assume_shape_base_type --emit-cpp "assume_shape value expects Tensor\\[f32\\], got Tensor\\[i32\\]\\[dyn, 2\\]"
expect_fail bad_generic_duplicate_param --check "duplicate generic parameter: T"
expect_fail bad_generic_function_arg_type --check "argument 1 for identity\\[i32\\] expects i32, got bool"
expect_fail bad_generic_inferred_conflict --check "conflicting inferred type argument T: i32 vs f64 for choose"
expect_fail bad_generic_box_arg_type --check "constructor Box\\[i32\\] argument 1 expects i32, got bool"
expect_fail bad_generic_method_arg_type --check "argument 1 for box.id\\[i32\\] expects i32, got bool"
expect_fail bad_generic_method_arity --check "method Box.choose expects 2 type arguments, got 1"
expect_fail bad_generic_pair_arg_type --check "constructor Pair\\[str, i32\\] argument 1 expects str, got i32"
expect_fail bad_generic_value_param_as_type --check "unknown field type: N"
expect_fail bad_generic_instantiated_operator --check "operator + expects Player, got Player while instantiating add\\[Player\\]"
expect_fail bad_generic_instantiated_method_operator --check "operator + expects Player, got Player while instantiating Ops.add\\[Player\\]"
expect_fail bad_generic_class_instantiated_method_operator --check "operator + expects Player, got Player while instantiating Pair\\[Player\\].add"
expect_fail bad_generic_class_instantiated_constructor_operator --check "operator + expects Player, got Player while instantiating Box\\[Player\\].init"
expect_fail bad_static_generic_instantiated_method_operator --check "operator + expects Player, got Player while instantiating Ops.add\\[Player\\]"
expect_fail bad_imported_generic_instantiated_method_operator --check "operator + expects Player, got Player while instantiating Box\\[Player\\].add"
expect_fail bad_imported_generic_instantiated_function_operator --check "operator + expects Player, got Player while instantiating combine\\[Player\\]"
expect_fail bad_imported_generic_explicit_function_operator --check "operator + expects Player, got Player while instantiating combine\\[Player\\]"
expect_fail bad_imported_generic_qualified_function_operator --check "operator + expects Player, got Player while instantiating combine\\[Player\\]"
expect_fail bad_imported_generic_overloaded_function_operator --check "operator + expects Player, got Player while instantiating overloaded\\[Player\\]"
expect_fail bad_generic_class_instantiated_binary_operator --check "operator + expects Player, got Player while instantiating Value\\[Player\\].add"
expect_fail bad_generic_instantiated_binary_operator --check "operator + expects Player, got Player while instantiating Ops.add\\[Player\\]"
expect_fail bad_generic_class_instantiated_index_operator --check "operator + expects Player, got Player while instantiating Value\\[Player\\].at"
expect_fail bad_generic_class_instantiated_index_assignment_operator --check "operator + expects Player, got Player while instantiating Value\\[Player\\].set_at"
expect_fail bad_generic_class_instantiated_call_operator --check "operator + expects Player, got Player while instantiating Value\\[Player\\].call"
expect_fail bad_empty_template_call --emit-cpp "template call expects at least 1 type argument"
expect_fail bad_non_callable_template_call --emit-cpp "unsupported template call expression: 1"
expect_fail bad_condition_type --emit-cpp "condition must be bool, got i32"
expect_fail bad_debug_assert_condition --emit-cpp "condition must be bool, got i32"
if "$repo_root/build/dudu" "$repo_root/tests/fixtures/bad_freestanding_assert.dd" \
    --emit-cpp "$repo_root/build/bad_freestanding_assert.out" -DTARGET_MODE=freestanding \
    2>"$repo_root/build/bad_freestanding_assert.err"; then
    echo "bad_freestanding_assert unexpectedly passed" >&2
    exit 1
fi
grep -q "runtime assert is not available in freestanding target mode" \
    "$repo_root/build/bad_freestanding_assert.err"
if "$repo_root/build/dudu" "$repo_root/tests/fixtures/bad_freestanding_assert.dd" \
    --emit-cpp "$repo_root/build/bad_embedded_assert.out" -DTARGET_MODE=embedded \
    2>"$repo_root/build/bad_embedded_assert.err"; then
    echo "bad_embedded_assert unexpectedly passed" >&2
    exit 1
fi
grep -q "runtime assert is not available in embedded target mode" \
    "$repo_root/build/bad_embedded_assert.err"
expect_fail bad_for_binding_type --emit-cpp "loop binding expects bool, got i32"
expect_fail bad_nested_for_type --emit-cpp "unknown loop binding type: MissingType"
expect_fail bad_for_non_container --emit-cpp "cannot iterate non-container: value"
expect_fail bad_build_flag --check "unknown build flag: build.NOPE"
expect_fail bad_implicit_cast --emit-cpp "cannot assign i32 to i64 without an explicit cast"
expect_fail bad_binary_bool --emit-cpp "operator + expects i32, got bool"
expect_fail bad_binary_mixed_width --emit-cpp "cannot assign i64 to i32 without an explicit cast"
expect_fail bad_binary_bool_pair --emit-cpp "operator + expects bool, got bool"
expect_fail bad_binary_string_subtract --emit-cpp "operator - expects str, got str"
expect_fail bad_bitwise_string --emit-cpp "operator | expects str, got str"
expect_fail bad_cpp_operator_mismatch --emit-cpp "operator + expects dudu_op.Vec2, got str"
expect_fail bad_dudu_operator_rhs --emit-cpp "operator + expects Vec2, got i32"
expect_fail bad_dudu_operator_static --check "operator methods cannot be static"
expect_fail bad_call_operator_static --check "operator methods cannot be static"
expect_fail bad_non_callable_value --emit-cpp "NotCallable is not callable; define @operator"
expect_fail bad_dudu_operator_non_string --check "@operator requires exactly one string literal argument"
expect_fail bad_section_non_string --check "@section requires exactly one string literal argument"
expect_fail bad_should_panic_non_string --check "@test.should_panic requires exactly one string literal argument"
expect_fail bad_dunder_operator --check "reserved Python-style dunder method name: __add__"
expect_fail bad_dunder_init --check "reserved Python-style dunder method name: __init__"
expect_fail bad_dunder_del --check "reserved Python-style dunder method name: __del__"
expect_fail bad_dunder_free --check "reserved Python-style dunder function name: __add__"
expect_fail bad_compound_assignment_type --emit-cpp "operator + expects i32, got bool"
expect_fail bad_shift_bool --emit-cpp "cannot assign i32 to bool without an explicit cast"
expect_fail bad_comparison_bool_order --emit-cpp "comparison < expects bool, got bool"
expect_fail bad_logical_operand --emit-cpp "and expects bool, got i32"
expect_fail bad_unary_missing_operand --emit-cpp "operator not expects an operand"
expect_fail bad_unary_bitwise_not_string --emit-cpp "~ expects integer, got str"
expect_fail bad_binary_missing_operand --emit-cpp "operator + expects left and right operands"
expect_fail bad_conditional_missing_value --emit-cpp "unsupported Python feature: conditional expressions"
expect_fail bad_len_arity --emit-cpp "len expects 1 arguments, got 0"
expect_fail bad_min_type --emit-cpp "min argument 2 expects i32, got bool"
expect_fail bad_lambda_body --emit-cpp "unsupported Python feature: lambda"
expect_fail bad_standalone_slice --emit-cpp "slice expression must be used inside an index"
expect_fail bad_unknown_expression --emit-cpp "unsupported expression: value ?? 1"
expect_fail bad_const_assignment --emit-cpp "cannot assign to constant: LIMIT"
expect_fail bad_except_binding --emit-cpp "expected except binding as name: Type"
expect_fail bad_finally --check "use RAII object lifetime or explicit cleanup"
expect_fail bad_yield --check "explicit iterator/state type or callback"
expect_fail bad_yield_expr --check "explicit iterator/state type or callback"
expect_fail bad_await --check "explicit callbacks, threads, or imported native async APIs"
expect_fail bad_list_comprehension --check "use an explicit loop"
expect_fail bad_dict_comprehension --check "use an explicit loop"
expect_fail bad_set_comprehension --check "use an explicit loop"
expect_fail bad_generator_expression --check "use an explicit loop"
expect_fail bad_with --check "rely on RAII object lifetime"
expect_fail bad_eval --check "dynamic execution is not part of Dudu"
expect_fail bad_exec --check "dynamic execution is not part of Dudu"
expect_fail bad_getattr --check "use statically known fields or methods"
expect_fail bad_setattr --check "use statically known fields or methods"
expect_fail bad_del --check "use explicit container removal or delete/free"
expect_fail bad_local_import --check "keep imports at module scope"
expect_fail bad_local_def --check "move the function to top-level or class scope"
expect_fail bad_def_expression_assign --check "move the function to top-level or class scope"
expect_fail bad_def_expression_return --check "move the function to top-level or class scope"
expect_fail bad_def_expression_arg --check "move the function to top-level or class scope"
expect_fail bad_return_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_append_local_address --emit-cpp "cannot let local address escape: value"
expect_fail bad_push_back_local_address --emit-cpp "cannot let local address escape: node"
expect_fail bad_static_assert --check "static_assert failed: PIXELS == 65"
expect_fail bad_static_compare --check "static_assert failed: PIXELS < 64"
expect_fail bad_const_calls_runtime --check "compile-time expression calls non-constexpr function: runtime_value"
expect_fail bad_static_assert_calls_runtime --check "compile-time expression calls non-constexpr function: runtime_value"
expect_fail bad_staticmethod_self --check "@staticmethod is not supported"
expect_fail bad_staticmethod_free --check "@staticmethod is not supported"
expect_fail bad_classmethod --check "@classmethod is not supported"
expect_fail bad_property --check "@property is not supported"
expect_fail bad_static_member --check "unknown static member: Color.MISSING"
expect_fail bad_class_static_outside_class --emit-cpp "class static access outside class"
expect_fail bad_self_static_field --check "unknown field: self.count"
expect_fail bad_call_arity --emit-cpp "function add expects 2 arguments, got 1"
expect_fail bad_call_type --emit-cpp "argument 1 for negate expects i32, got bool"
expect_fail bad_non_callable_call --emit-cpp "unsupported call expression: 1"
expect_fail bad_callback_lambda --emit-cpp "unsupported Python feature: lambda"
expect_fail bad_fn_pointer_call --emit-cpp "function callback expects 1 arguments, got 0"
expect_fail bad_cpp_std_function_call --emit-cpp "argument 1 for callback expects i32, got bool"
expect_fail bad_tuple_arity --check "tuple supports 1 to 8 elements, got 9"
expect_fail bad_function_decorator --check "unknown function decorator: @cuda.glboal"
expect_fail bad_class_decorator --check "unknown class decorator: @packd"
expect_fail bad_class_member_visibility --check "explicit visibility keywords are not supported"
expect_fail bad_extern_c_str --check "@extern_c return type is not C ABI safe: str"

if (
    cd "$repo_root/tests/fixtures/bad_target_decorator_mode"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_target_decorator_mode.err"; then
    echo "bad_target_decorator_mode unexpectedly passed" >&2
    exit 1
fi
grep -q '@cuda.global requires \[target\] mode = "cuda"' \
    "$repo_root/build/bad_target_decorator_mode.err"

if (
    cd "$repo_root/tests/fixtures/bad_shader_target_mode"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_shader_target_mode.err"; then
    echo "bad_shader_target_mode unexpectedly passed" >&2
    exit 1
fi
grep -q '@shader.compute requires \[target\] mode = "shader"' \
    "$repo_root/build/bad_shader_target_mode.err"

rm -rf "$repo_root/build/cmake-backend"
if "$repo_root/build/duc" build "$repo_root/tests/fixtures/bad_native_build.dd" -o "$repo_root/build/bad_native_build" 2>"$repo_root/build/bad_native_build.err"; then echo "bad_native_build unexpectedly passed" >&2; exit 1; fi
grep -q "CMake build failed" "$repo_root/build/bad_native_build.err"
grep -q "generated/bad_native_build.cpp" "$repo_root/build/bad_native_build.err"
grep -q 'this is not valid c++;' "$repo_root/build/bad_native_build.err"
grep -q "command: " "$repo_root/build/bad_native_build.err"
grep -q "output:" "$repo_root/build/bad_native_build.err"

rm -rf "$repo_root/build/cmake-backend"
if "$repo_root/build/duc" build "$repo_root/tests/fixtures/bad_missing_header.dd" -o "$repo_root/build/bad_missing_header" 2>"$repo_root/build/bad_missing_header.err"; then echo "bad_missing_header unexpectedly passed" >&2; exit 1; fi
grep -q "dudu_missing_header_for_test.hpp" "$repo_root/build/bad_missing_header.err"
grep -q "could not scan native header" "$repo_root/build/bad_missing_header.err"
grep -q "hint: add the header directory to \\[include\\].paths or the package to \\[pkg\\].libs" \
    "$repo_root/build/bad_missing_header.err"

if CLANGXX="$repo_root/build/not-a-clang" "$repo_root/build/duc" build \
    "$repo_root/tests/fixtures/native_scan_local.dd" -o "$repo_root/build/bad_clangxx" \
    2>"$repo_root/build/bad_clangxx.err"; then
    echo "bad_clangxx unexpectedly passed" >&2
    exit 1
fi
grep -q "could not scan native header" "$repo_root/build/bad_clangxx.err"
grep -q "hint: native header awareness requires clang++" "$repo_root/build/bad_clangxx.err"

if "$repo_root/build/duc" emit "$repo_root/tests/fixtures/bad_package_build/main.dd" \
    -o "$repo_root/build/bad_package_build.cpp" 2>"$repo_root/build/bad_package_build.err"; then
    echo "bad_package_build unexpectedly passed" >&2
    exit 1
fi
grep -q "invalid \\[build\\] entry" "$repo_root/build/bad_package_build.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_target"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_project_target.err"; then
    echo "bad_project_target unexpectedly passed" >&2
    exit 1
fi
grep -q "invalid \\[target\\] mode" "$repo_root/build/bad_project_target.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_unknown_key"
    "$repo_root/build/duc" check
) 2>"$repo_root/build/bad_project_unknown_key.err"; then
    echo "bad_project_unknown_key unexpectedly passed" >&2
    exit 1
fi
grep -q "unknown \\[cc\\] entry" "$repo_root/build/bad_project_unknown_key.err"

if "$repo_root/build/dudu" \
    "$repo_root/tests/fixtures/project_backend_cmake_namespaces/main.dd" \
    --emit-cpp "$repo_root/build/bad_merged_module_namespace.cpp" \
    2>"$repo_root/build/bad_merged_module_namespace.err"; then
    echo "bad_merged_module_namespace unexpectedly passed" >&2
    exit 1
fi
grep -q "duplicate declaration: Box" "$repo_root/build/bad_merged_module_namespace.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_transitive_import_leak"
    "$repo_root/build/dudu" build
) 2>"$repo_root/build/bad_project_transitive_import_leak.err"; then
    echo "bad_project_transitive_import_leak unexpectedly passed" >&2
    exit 1
fi
grep -q "unknown function: leaf.hidden_answer" \
    "$repo_root/build/bad_project_transitive_import_leak.err"

if (
    cd "$repo_root/tests/fixtures/bad_project_selective_import_reexport_leak"
    "$repo_root/build/dudu" build
) 2>"$repo_root/build/bad_project_selective_import_reexport_leak.err"; then
    echo "bad_project_selective_import_reexport_leak unexpectedly passed" >&2
    exit 1
fi
grep -q "module 'facade' has no exported function 'hidden_answer'" \
    "$repo_root/build/bad_project_selective_import_reexport_leak.err"
