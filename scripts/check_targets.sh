#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target_dir="$repo_root/tests/targets/tensor_indexing"
manifest="$target_dir/manifest.tsv"
dudu_bin="$repo_root/build/dudu"

if [[ ! -x "$dudu_bin" ]]; then
    "$repo_root/scripts/build.sh" >/dev/null
fi

pass_count=0
xfail_count=0
failure_count=0

while IFS=$'\t' read -r file status expected; do
    [[ -z "${file:-}" || "$file" == \#* ]] && continue

    path="$target_dir/$file"
    base_name="target_${file//[\/.]/_}"
    log="$repo_root/build/$base_name.log"
    cpp="$repo_root/build/$base_name.cpp"

    if [[ ! -f "$path" ]]; then
        echo "missing target fixture: $path" >&2
        failure_count=$((failure_count + 1))
        continue
    fi

    set +e
    "$dudu_bin" "$path" --emit-cpp "$cpp" >"$log" 2>&1
    code=$?
    set -e

    case "$status" in
        pass)
            if [[ "$code" -ne 0 ]]; then
                echo "FAIL $file: expected pass" >&2
                cat "$log" >&2
                failure_count=$((failure_count + 1))
            else
                echo "PASS $file"
                pass_count=$((pass_count + 1))
            fi
            ;;
        run)
            if [[ "$code" -ne 0 ]]; then
                echo "FAIL $file: expected emit before run" >&2
                cat "$log" >&2
                failure_count=$((failure_count + 1))
                continue
            fi
            exe="$repo_root/build/$base_name"
            compile_log="$repo_root/build/$base_name.compile.log"
            set +e
            c++ -std=c++20 -I "$target_dir" "$cpp" -o "$exe" >"$compile_log" 2>&1
            compile_code=$?
            set -e
            if [[ "$compile_code" -ne 0 ]]; then
                echo "FAIL $file: generated C++ did not compile" >&2
                cat "$compile_log" >&2
                failure_count=$((failure_count + 1))
                continue
            fi
            set +e
            "$exe" >"$log.run" 2>&1
            run_code=$?
            set -e
            if [[ "$run_code" -ne "${expected:-0}" ]]; then
                echo "FAIL $file: expected exit ${expected:-0}, got $run_code" >&2
                cat "$log.run" >&2
                failure_count=$((failure_count + 1))
            else
                echo "RUN $file"
                pass_count=$((pass_count + 1))
            fi
            ;;
        xfail)
            if [[ "$code" -eq 0 ]]; then
                echo "XPASS $file: promote or tighten this target" >&2
                failure_count=$((failure_count + 1))
            elif [[ -n "${expected:-}" ]] && ! grep -Fq "$expected" "$log"; then
                echo "FAIL $file: diagnostic did not contain '$expected'" >&2
                cat "$log" >&2
                failure_count=$((failure_count + 1))
            else
                echo "XFAIL $file"
                xfail_count=$((xfail_count + 1))
            fi
            ;;
        *)
            echo "unknown target status '$status' for $file" >&2
            failure_count=$((failure_count + 1))
            ;;
    esac
done <"$manifest"

echo "target summary: $pass_count pass, $xfail_count xfail, $failure_count fail"

if [[ "$failure_count" -ne 0 ]]; then
    exit 1
fi
