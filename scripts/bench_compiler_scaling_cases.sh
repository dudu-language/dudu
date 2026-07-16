# shellcheck shell=bash

prepare_scaled_emission_sample() {
    rm -rf "$scaled_emission_output"
}

record_scaled_emission_size() {
    local case_name="$1"
    local source_path="$2"
    local generated_files generated_lines generated_bytes
    generated_files="$(find "$scaled_emission_output" -type f \
        \( -name '*.cpp' -o -name '*.hpp' \) | wc -l | tr -d ' ')"
    generated_lines="$(find "$scaled_emission_output" -type f \
        \( -name '*.cpp' -o -name '*.hpp' \) -exec wc -l {} + | awk 'END { print $1 + 0 }')"
    generated_bytes="$(find "$scaled_emission_output" -type f \
        \( -name '*.cpp' -o -name '*.hpp' \) -exec wc -c {} + | awk 'END { print $1 + 0 }')"
    printf '%s,%s,%s,%s,%s\n' "$case_name" "$(line_count "$source_path")" \
        "$generated_files" "$generated_lines" "$generated_bytes" >>"$emission_sizes_path"
}

run_scaled_emission_case() {
    local shape="$1"
    local requested_lines="$2"
    local entry="$3"
    if [[ "$measure_emission" -ne 1 ]]; then
        return
    fi
    local source_path
    source_path="$(dirname "$entry")"
    scaled_emission_output="$bench_dir/scaled_emission/${shape}_${requested_lines}"
    run_case_prepared "duc_emit_${shape}_${requested_lines}" "cpp_emit_${shape}" \
        "$source_path" prepare_scaled_emission_sample \
        "$tool_build_dir/duc" emit-modules "$entry" -o "$scaled_emission_output" --quiet
    record_scaled_emission_size "${shape}_${requested_lines}" "$source_path"
}

run_scaled_shape() {
    local shape="$1"
    local requested_lines="$2"
    local prepare_fn="$3"
    local entry source_path
    entry="$($prepare_fn "$requested_lines")"
    source_path="$entry"
    if [[ "$shape" == "modules" || "$shape" == "native" || "$shape" == "mixed" ]]; then
        source_path="$(dirname "$entry")"
    fi
    run_case "duc_check_${shape}_${requested_lines}" "frontend_${shape}" "$source_path" \
        "$tool_build_dir/duc" check "$entry"
    run_scaled_emission_case "$shape" "$requested_lines" "$entry"
}

run_scaling_benchmarks() {
    emission_sizes_path="${csv_path%.csv}_emission_sizes.csv"
    if [[ "$measure_emission" -eq 1 ]]; then
        printf 'case,source_lines,generated_files,generated_lines,generated_bytes\n' \
            >"$emission_sizes_path"
    fi

    local requested_lines shape prepare_fn
    IFS=',' read -r -a requested_line_scales <<<"$line_scales"
    for requested_lines in "${requested_line_scales[@]}"; do
        requested_lines="$(printf '%s' "$requested_lines" | tr -d '[:space:]')"
        if [[ -z "$requested_lines" ]]; then
            continue
        fi
        case "$requested_lines" in
            *[!0-9]*)
                echo "invalid --line-scales entry: $requested_lines" >&2
                exit 1
                ;;
        esac
        for shape in "${requested_shapes[@]}"; do
            shape="$(printf '%s' "$shape" | tr -d '[:space:]')"
            case "$shape" in
                functions) prepare_fn=prepare_scaled_functions_case ;;
                classes) prepare_fn=prepare_scaled_classes_case ;;
                inheritance) prepare_fn=prepare_scaled_inheritance_case ;;
                expressions) prepare_fn=prepare_scaled_expressions_case ;;
                modules) prepare_fn=prepare_scaled_modules_case ;;
                calls) prepare_fn=prepare_scaled_calls_case ;;
                control) prepare_fn=prepare_scaled_control_case ;;
                arrays) prepare_fn=prepare_scaled_arrays_case ;;
                indexing) prepare_fn=prepare_scaled_indexing_case ;;
                generics) prepare_fn=prepare_scaled_generics_case ;;
                matches) prepare_fn=prepare_scaled_matches_case ;;
                operators) prepare_fn=prepare_scaled_operators_case ;;
                native) prepare_fn=prepare_scaled_native_case ;;
                stdlib) prepare_fn=prepare_scaled_stdlib_case ;;
                mixed) prepare_fn=prepare_scaled_mixed_case ;;
            esac
            run_scaled_shape "$shape" "$requested_lines" "$prepare_fn"
        done
    done
}
