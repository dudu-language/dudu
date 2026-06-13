#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project="$repo_root/tests/fixtures/project_plugin_dynamic_library"
lib="$repo_root/build/libdudu_plugin.so"
header="$repo_root/build/dudu_plugin.h"
host_c="$repo_root/build/dudu_plugin_host.c"
host="$repo_root/build/dudu_plugin_host"

(
    cd "$project"
    "$repo_root/build/duc" build -o "$lib" --verbose \
        2>"$repo_root/build/project_plugin_dynamic_library_verbose.err"
    "$repo_root/build/duc" main.dd --emit-c-header "$header"
)
grep -q -- "-fPIC -shared" "$repo_root/build/project_plugin_dynamic_library_verbose.err"
grep -q "int32_t plugin_answer(void);" "$header"

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

cc -std=c11 -I"$repo_root/build" "$host_c" -ldl -o "$host"
set +e
"$host" "$lib"
status=$?
set -e
if [[ "$status" -ne 42 ]]; then
    echo "dudu_plugin_host returned $status, expected 42" >&2
    exit 1
fi
