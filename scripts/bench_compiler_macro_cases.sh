# shellcheck shell=bash

macro_bench_root="$bench_dir/macro_projects"

write_macro_package() {
    local project="$1"
    mkdir -p "$project"
    cat >"$project/dudu.toml" <<'EOF'
name = "macro_benchmark"
entry = "main.dd"
build_dir = "build"
EOF
    cat >"$project/macros.dd" <<'EOF'
import dudu.ast as ast


class DebugOptions:
    label: str = ""


@macro
def NoOp(item: ast.ClassDecl) -> ast.Expansion:
    return ast.expansion()


@macro(attributes=DebugOptions)
def Debug(item: ast.ClassDecl) -> ast.Expansion:
    count: i32 = 0
    for field in item.fields:
        count += 1
    self_param = ast.Parameter(
        name="self",
        type=ast.reference_type(ast.named_type("Self")),
    )
    method = ast.FunctionDecl(
        name="debug_field_count",
        parameters=[self_param],
        return_type=ast.named_type("i32"),
        body=[ast.return_statement(ast.int_expression(str(count)))],
    )
    out = ast.expansion()
    out.add_method(method)
    return out
EOF
}

write_macro_consumers() {
    local project="$1"
    local count="$2"
    local macro_name="$3"
    {
        printf 'from macros import %s\n\n' "$macro_name"
        for ((index = 0; index < count; ++index)); do
            printf '@derive(%s)\n' "$macro_name"
            printf 'class BenchType%d:\n' "$index"
            if [[ "$macro_name" == "Debug" && "$index" -eq 0 ]]; then
                printf '    @Debug(label="base")\n'
            fi
            printf '    value: i32\n'
            printf '    weight: f32\n\n'
        done
        printf 'def unrelated(value: i32) -> i32:\n'
        printf '    return value + 1\n'
    } >"$project/main.dd"
}

write_handwritten_consumers() {
    local project="$1"
    local count="$2"
    {
        for ((index = 0; index < count; ++index)); do
            printf 'class BenchType%d:\n' "$index"
            printf '    value: i32\n'
            printf '    weight: f32\n\n'
            printf '    def debug_field_count(self) -> i32:\n'
            printf '        return 2\n\n'
        done
        printf 'def unrelated(value: i32) -> i32:\n'
        printf '    return value + 1\n'
    } >"$project/handwritten.dd"
}

clear_macro_expansions() {
    local project="$1"
    rm -rf "$project/build/.dudu/macros/expansions"
}

prepare_macro_cold() {
    rm -rf "$macro_prepare_project/build/.dudu/macros"
}

prepare_noop_execution() {
    clear_macro_expansions "$macro_prepare_project"
}

prepare_debug_execution() {
    clear_macro_expansions "$macro_prepare_project"
}

prepare_unrelated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/return value \+ 1/return value + 2/' "$macro_prepare_project/main.dd"
    else
        perl -0pi -e 's/return value \+ 2/return value + 1/' "$macro_prepare_project/main.dd"
    fi
}

prepare_helper_edit() {
    local sample="$1"
    perl -0pi -e 's/@Debug\(label="[^"]*"\)/@Debug(label="sample_'"$sample"'")/' \
        "$macro_prepare_project/main.dd"
}

run_macro_scale() {
    local count="$1"
    local project="$macro_bench_root/debug_$count"
    rm -rf "$project"
    write_macro_package "$project"
    write_macro_consumers "$project" "$count" "Debug"
    write_handwritten_consumers "$project" "$count"

    macro_prepare_project="$project"
    run_case_prepared "macro_debug_${count}_cold" "macro_cold" "$project" \
        prepare_macro_cold \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"

    run_case_prepared "macro_debug_${count}_execute" "macro_execute" "$project" \
        prepare_debug_execution \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"

    "$tool_build_dir/duc" expand "$project/main.dd" -o "$project/expanded.dd" >/dev/null
    run_case "macro_debug_${count}_cached" "macro_cached" "$project" \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"

    run_case_prepared "macro_debug_${count}_unrelated" "macro_incremental_unrelated" "$project" \
        prepare_unrelated_edit \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"

    run_case_prepared "macro_debug_${count}_helper" "macro_incremental_helper" "$project" \
        prepare_helper_edit \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"

    run_case "macro_debug_${count}_handwritten" "macro_handwritten" "$project/handwritten.dd" \
        "$tool_build_dir/duc" check "$project/handwritten.dd"
}

run_noop_scale() {
    local count="$1"
    local project="$macro_bench_root/noop_$count"
    rm -rf "$project"
    write_macro_package "$project"
    write_macro_consumers "$project" "$count" "NoOp"
    "$tool_build_dir/duc" expand "$project/main.dd" -o "$project/expanded.dd" >/dev/null
    macro_prepare_project="$project"
    run_case_prepared "macro_noop_${count}_execute" "macro_execute_noop" "$project" \
        prepare_noop_execution \
        "$tool_build_dir/duc" expand "$project/main.dd" --timings -o "$project/expanded.dd"
}

report_macro_comparisons() {
    local requested="$1"
    [[ -z "$requested" ]] && return
    local language tool
    IFS=',' read -r -a languages <<<"$requested"
    for language in "${languages[@]}"; do
        language="$(printf '%s' "$language" | tr -d '[:space:]')"
        case "$language" in
            rust) tool="cargo" ;;
            csharp) tool="dotnet" ;;
            swift) tool="swiftc" ;;
            nim) tool="nim" ;;
            *)
                echo "unsupported macro comparison: $language" >&2
                return 1
                ;;
        esac
        if command -v "$tool" >/dev/null 2>&1; then
            echo "macro comparison adapter available: $language ($tool)"
        else
            echo "macro comparison skipped: $language ($tool not installed)"
        fi
    done
}

run_macro_benchmarks() {
    local scales="$1"
    local comparisons="$2"
    rm -rf "$macro_bench_root"
    mkdir -p "$macro_bench_root"
    IFS=',' read -r -a requested_macro_scales <<<"$scales"
    local count
    for count in "${requested_macro_scales[@]}"; do
        count="$(printf '%s' "$count" | tr -d '[:space:]')"
        case "$count" in
            ''|*[!0-9]*)
                echo "invalid --macro-scales entry: $count" >&2
                return 1
                ;;
        esac
        if [[ "$count" -lt 1 ]]; then
            echo "macro scales must be positive" >&2
            return 1
        fi
        run_noop_scale "$count"
        run_macro_scale "$count"
    done
    report_macro_comparisons "$comparisons"
}
