# shellcheck shell=bash

write_nim_macro_comparison() {
    local project="$1"
    local count="$2"
    mkdir -p "$project"
    cat >"$project/debug_count_macros.nim" <<'EOF'
import macros

macro debugCount*(target: typedesc): untyped =
  let declaration = target.getTypeInst[1].getImpl
  proc countFields(node: NimNode): int =
    if node.kind == nnkIdentDefs:
      return node.len - 2
    for child in node:
      result += countFields(child)
  let fieldCount = countFields(declaration)
  result = quote do:
    proc debugFieldCount*(value: `target`): int32 =
      int32(`fieldCount`)
EOF
    {
        printf 'import debug_count_macros\n\n'
        for ((index = 0; index < count; ++index)); do
            printf 'type BenchType%d = object\n' "$index"
            printf '  value: int32\n'
            printf '  weight: float32\n'
            printf 'debugCount(BenchType%d)\n\n' "$index"
        done
        printf 'proc unrelated(value: int32): int32 = value + 1\n\n'
        printf 'let value = BenchType0(value: 1, weight: 2.0)\n'
        printf 'echo debugFieldCount(value) + unrelated(value.value)\n'
    } >"$project/main.nim"
    {
        for ((index = 0; index < count; ++index)); do
            printf 'type BenchType%d = object\n' "$index"
            printf '  value: int32\n'
            printf '  weight: float32\n'
            printf 'proc debugFieldCount(value: BenchType%d): int32 = 2\n\n' "$index"
        done
        printf 'proc unrelated(value: int32): int32 = value + 1\n\n'
        printf 'let value = BenchType0(value: 1, weight: 2.0)\n'
        printf 'echo debugFieldCount(value) + unrelated(value.value)\n'
    } >"$project/handwritten.nim"
    cat >"$project/package_probe.nim" <<'EOF'
import debug_count_macros

discard
EOF
}

prepare_nim_package_clean() {
    rm -rf "$nim_prepare_project/nimcache_package" "$nim_prepare_project/package_probe"
}

prepare_nim_app_clean() {
    rm -rf "$nim_prepare_project/nimcache_app" "$nim_prepare_project/main"
}

prepare_nim_handwritten_clean() {
    rm -rf "$nim_prepare_project/nimcache_handwritten" "$nim_prepare_project/handwritten"
}

prepare_nim_unrelated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/value \+ 1/value + 2/' "$nim_prepare_project/main.nim"
    else
        perl -0pi -e 's/value \+ 2/value + 1/' "$nim_prepare_project/main.nim"
    fi
}

prepare_nim_decorated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/type BenchType0 = object\n  value: int32\n  weight: float32/type BenchType0 = object\n  value: int32\n  weight: float32\n  extra: int32/' \
            "$nim_prepare_project/main.nim"
    else
        perl -0pi -e 's/type BenchType0 = object\n  value: int32\n  weight: float32\n  extra: int32/type BenchType0 = object\n  value: int32\n  weight: float32/' \
            "$nim_prepare_project/main.nim"
    fi
}

run_nim_macro_comparison() {
    local comparison_root="$macro_bench_root/compare_nim"
    local count project
    rm -rf "$comparison_root"
    for count in 100 1000; do
        project="$comparison_root/$count"
        write_nim_macro_comparison "$project" "$count"
        nim_prepare_project="$project"
        if [[ "$count" -eq 100 ]]; then
            run_case_prepared "nim_macro_package_clean" "macro_compare_nim_package" \
                "$project/debug_count_macros.nim" prepare_nim_package_clean \
                nim c -d:release --hints:off --verbosity:0 \
                --nimcache:"$project/nimcache_package" -o:"$project/package_probe" \
                "$project/package_probe.nim"
        fi
        run_case_prepared "nim_macro_debug_${count}_clean" "macro_compare_nim_clean" \
            "$project/main.nim" prepare_nim_app_clean \
            nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_app" -o:"$project/main" "$project/main.nim"
        nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_app" -o:"$project/main" "$project/main.nim" \
            >/dev/null
        run_case "nim_macro_debug_${count}_warm" "macro_compare_nim_warm" \
            "$project/main.nim" nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_app" -o:"$project/main" "$project/main.nim"
        run_case_prepared "nim_macro_debug_${count}_unrelated" \
            "macro_compare_nim_unrelated" "$project/main.nim" prepare_nim_unrelated_edit \
            nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_app" -o:"$project/main" "$project/main.nim"
        run_case_prepared "nim_macro_debug_${count}_decorated" \
            "macro_compare_nim_decorated" "$project/main.nim" prepare_nim_decorated_edit \
            nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_app" -o:"$project/main" "$project/main.nim"
        run_case_prepared "nim_macro_debug_${count}_handwritten" \
            "macro_compare_nim_handwritten" "$project/handwritten.nim" \
            prepare_nim_handwritten_clean nim c -d:release --hints:off --verbosity:0 \
            --nimcache:"$project/nimcache_handwritten" -o:"$project/handwritten" \
            "$project/handwritten.nim"
    done
}
