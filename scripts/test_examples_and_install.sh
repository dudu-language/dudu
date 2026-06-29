#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$repo_root/scripts/test_helpers.sh"

required_examples=(
    allocators.dd
    compile_time.dd
    cpp_library.dd
    cuda_kernel.dd
    cuda_shared_memory_tile.dd
    function_pointers.dd
    interrupt_handler.dd
    layout_hardware.dd
    macro_bomb.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
)

for example in "${required_examples[@]}"; do
    test -f "$repo_root/examples/$example"
    "$repo_root/build/dudu" "$repo_root/examples/$example" --check
done

object_examples=(
    allocators.dd
    compile_time.dd
    cpp_library.dd
    function_pointers.dd
    layout_hardware.dd
    modules_visibility.dd
    native_escape.dd
    numerics_kmeans.dd
    shader_compute.dd
    systems_mmap.dd
    threading_atomics.dd
)

for example in "${object_examples[@]}"; do
    compile_example_object "$example"
done

test -f "$repo_root/editors/vscode/extension.js"
test -f "$repo_root/editors/vscode/syntaxes/dudu.tmLanguage.json"
test -f "$repo_root/editors/vim/syntax/dudu.vim"
test -f "$repo_root/editors/nvim/queries/dudu/highlights.scm"
grep -q '"command": "dudu.fmtFile"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.checkFile"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.buildProject"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.runFile"' "$repo_root/editors/vscode/package.json"
grep -q '"command": "dudu.testProject"' "$repo_root/editors/vscode/package.json"
grep -q 'registerCommand("dudu.fmtFile"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.checkFile"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.buildProject"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.runFile"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCommand("dudu.testProject"' "$repo_root/editors/vscode/extension.js"
grep -q 'registerCompletionItemProvider("dudu"' "$repo_root/editors/vscode/extension.js"
grep -q 'createDiagnosticCollection("dudu")' "$repo_root/editors/vscode/extension.js"
grep -q '"onLanguage:dudu"' "$repo_root/editors/vscode/package.json"
grep -q '"dudu-lsp"' "$repo_root/editors/vscode/extension.js"
grep -q '"dudu.path"' "$repo_root/editors/vscode/package.json"
grep -q '"dudu.lspPath"' "$repo_root/editors/vscode/package.json"
grep -q "onDidSaveTextDocument" "$repo_root/editors/vscode/extension.js"
node --check "$repo_root/editors/vscode/extension.js"
bash -n "$repo_root/scripts/install-local.sh"
install_prefix="$repo_root/build/install-smoke-prefix"
rm -rf "$install_prefix"
cmake --install "$repo_root/build" --prefix "$install_prefix" >/dev/null
test -x "$install_prefix/bin/dudu"
test -x "$install_prefix/bin/duc"
test -x "$install_prefix/bin/dudu-lsp"
test -f "$install_prefix/share/dudu/editors/vscode/package.json"
test -f "$install_prefix/share/dudu/editors/vim/syntax/dudu.vim"
test -f "$install_prefix/share/doc/dudu/README.md"

generated_header="$repo_root/build/cpp_library.hpp"
"$repo_root/build/dudu" "$repo_root/examples/cpp_library.dd" --emit-header "$generated_header"
grep -q 'inline constexpr std::string_view TARGET_KIND = "executable";' "$generated_header"
grep -q 'inline constexpr std::string_view TARGET_MODE = "hosted";' "$generated_header"
printf '#include "cpp_library.hpp"\nint main() { return 0; }\n' >"$repo_root/build/header_smoke.cpp"
"${CXX:-c++}" -std=c++20 -I"$repo_root/build" -c "$repo_root/build/header_smoke.cpp" \
    -o "$repo_root/build/header_smoke.o"
