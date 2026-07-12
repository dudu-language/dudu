#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project="$repo_root/tests/fixtures/project_plugin_dynamic_library"
link_flag=""
case "$(uname -s)" in
Darwin)
    lib="$repo_root/build/libdudu_plugin.dylib"
    ;;
Linux)
    lib="$repo_root/build/libdudu_plugin.so"
    link_flag="-ldl"
    ;;
*)
    echo "unsupported dynamic-library smoke host: $(uname -s)" >&2
    exit 1
    ;;
esac
header="$repo_root/build/dudu_plugin.h"
host_c="$repo_root/build/dudu_plugin_host.c"
host="$repo_root/build/dudu_plugin_host"

rm -f "$lib" "$header" "$host_c" "$host"
rm -rf "$project/build"

echo "dynamic library smoke: build $lib"
(
    cd "$project"
    build_log="$repo_root/build/project_plugin_dynamic_library_verbose.err"
    if ! "$repo_root/build/duc" build -o "$lib" --verbose 2>"$build_log"; then
        echo "dynamic library build failed" >&2
        cat "$build_log" >&2
        exit 1
    fi
    if ! "$repo_root/build/duc" main.dd --emit-c-header "$header"; then
        echo "dynamic library C header emission failed" >&2
        exit 1
    fi
)
if [[ ! -f "$lib" ]]; then
    echo "dynamic library output is missing: $lib" >&2
    find "$repo_root/build" "$project/build" -maxdepth 3 -type f -print >&2
    exit 1
fi
if ! grep -q "add_library(dudu_plugin SHARED" \
    "$project/build/cmake-backend/source/CMakeLists.txt"; then
    echo "generated CMake is missing the shared-library target" >&2
    exit 1
fi
if ! grep -q "int32_t plugin_answer(void);" "$header"; then
    echo "generated C header is missing plugin_answer" >&2
    exit 1
fi

cat >"$host_c" <<'C'
#include "dudu_plugin.h"

#include <dlfcn.h>
#include <stdint.h>

typedef int32_t (*plugin_answer_fn)(void);

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    void* library = dlopen(argv[1], RTLD_NOW);
    if (library == 0) {
        return 3;
    }
    plugin_answer_fn answer = (plugin_answer_fn)dlsym(library, "plugin_answer");
    if (answer == 0) {
        dlclose(library);
        return 4;
    }
    int32_t value = answer();
    dlclose(library);
    return value;
}
C

echo "dynamic library smoke: compile and run host"
if [[ -n "$link_flag" ]]; then
    cc -std=c11 -I"$repo_root/build" "$host_c" "$link_flag" -o "$host"
else
    cc -std=c11 -I"$repo_root/build" "$host_c" -o "$host"
fi
set +e
"$host" "$lib"
status=$?
set -e
if [[ "$status" -ne 42 ]]; then
    echo "dudu_plugin_host returned $status, expected 42" >&2
    exit 1
fi
