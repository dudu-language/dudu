# shellcheck shell=bash

write_rust_macro_comparison() {
    local project="$1"
    local count="$2"
    mkdir -p "$project/macros/src" "$project/app/src"
    cat >"$project/Cargo.toml" <<'EOF'
[workspace]
members = ["macros", "app"]
resolver = "2"
EOF
    cat >"$project/macros/Cargo.toml" <<'EOF'
[package]
name = "benchmark_macros"
version = "0.0.0"
edition = "2021"

[lib]
proc-macro = true
EOF
    cat >"$project/macros/src/lib.rs" <<'EOF'
extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};

#[proc_macro_derive(DebugCount)]
pub fn derive_debug_count(input: TokenStream) -> TokenStream {
    let mut tokens = input.into_iter();
    let mut name = None;
    let mut fields = 0;
    while let Some(token) = tokens.next() {
        match token {
            TokenTree::Ident(ident) if ident.to_string() == "struct" => {
                if let Some(TokenTree::Ident(ident)) = tokens.next() {
                    name = Some(ident.to_string());
                }
            }
            TokenTree::Group(group) if name.is_some() => {
                fields = group
                    .stream()
                    .into_iter()
                    .filter(|item| matches!(item, TokenTree::Punct(punct) if punct.as_char() == ':'))
                    .count();
                break;
            }
            _ => {}
        }
    }
    let name = name.expect("DebugCount requires a struct");
    format!(
        "impl {name} {{ fn debug_field_count(&self) -> i32 {{ {fields} }} }}"
    )
    .parse()
    .expect("generated DebugCount implementation must parse")
}
EOF
    cat >"$project/app/Cargo.toml" <<'EOF'
[package]
name = "benchmark_app"
version = "0.0.0"
edition = "2021"

[dependencies]
benchmark_macros = { path = "../macros" }
EOF
    {
        printf '#![allow(dead_code)]\n'
        printf 'use benchmark_macros::DebugCount;\n\n'
        for ((index = 0; index < count; ++index)); do
            printf '#[derive(DebugCount)]\n'
            printf 'struct BenchType%d { value: i32, weight: f32 }\n\n' "$index"
        done
        printf 'fn unrelated(value: i32) -> i32 { value + 1 }\n\n'
        printf 'fn main() {\n'
        printf '    let value = BenchType0 { value: 1, weight: 2.0 };\n'
        printf '    std::hint::black_box(value.value);\n'
        printf '    std::hint::black_box(value.weight);\n'
        printf '    std::hint::black_box(value.debug_field_count());\n'
        printf '    std::hint::black_box(unrelated(1));\n'
        printf '}\n'
    } >"$project/app/src/main.rs"
    {
        printf '#![allow(dead_code)]\n'
        for ((index = 0; index < count; ++index)); do
            printf 'struct BenchType%d { value: i32, weight: f32 }\n' "$index"
            printf 'impl BenchType%d { fn debug_field_count(&self) -> i32 { 2 } }\n\n' "$index"
        done
        printf 'fn unrelated(value: i32) -> i32 { value + 1 }\n\n'
        printf 'fn main() {\n'
        printf '    let value = BenchType0 { value: 1, weight: 2.0 };\n'
        printf '    std::hint::black_box(value.value);\n'
        printf '    std::hint::black_box(value.weight);\n'
        printf '    std::hint::black_box(value.debug_field_count());\n'
        printf '    std::hint::black_box(unrelated(1));\n'
        printf '}\n'
    } >"$project/app/src/handwritten.rs"
}

prepare_rust_clean() {
    rm -rf "$rust_prepare_project/target"
}

prepare_rust_unrelated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/value \+ 1/value + 2/' "$rust_prepare_project/app/src/main.rs"
    else
        perl -0pi -e 's/value \+ 2/value + 1/' "$rust_prepare_project/app/src/main.rs"
    fi
}

prepare_rust_decorated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/struct BenchType0 \{ value: i32, weight: f32 \}/struct BenchType0 { value: i32, weight: f32, extra: i32 }/' \
            "$rust_prepare_project/app/src/main.rs"
        perl -0pi -e 's/BenchType0 \{ value: 1, weight: 2\.0 \}/BenchType0 { value: 1, weight: 2.0, extra: 3 }/' \
            "$rust_prepare_project/app/src/main.rs"
        perl -0pi -e 's/black_box\(value\.weight\);/black_box(value.weight);\n    std::hint::black_box(value.extra);/' \
            "$rust_prepare_project/app/src/main.rs"
    else
        perl -0pi -e 's/struct BenchType0 \{ value: i32, weight: f32, extra: i32 \}/struct BenchType0 { value: i32, weight: f32 }/' \
            "$rust_prepare_project/app/src/main.rs"
        perl -0pi -e 's/BenchType0 \{ value: 1, weight: 2\.0, extra: 3 \}/BenchType0 { value: 1, weight: 2.0 }/' \
            "$rust_prepare_project/app/src/main.rs"
        perl -0pi -e 's/\n    std::hint::black_box\(value\.extra\);//' \
            "$rust_prepare_project/app/src/main.rs"
    fi
}

run_rust_macro_comparison() {
    local comparison_root="$macro_bench_root/compare_rust"
    local count project
    rm -rf "$comparison_root"
    for count in 100 1000; do
        project="$comparison_root/$count"
        write_rust_macro_comparison "$project" "$count"
        rust_prepare_project="$project"
        if [[ "$count" -eq 100 ]]; then
            run_case_prepared "rust_macro_package_clean" "macro_compare_rust_package" \
                "$project/macros/src/lib.rs" prepare_rust_clean \
                cargo build --release --manifest-path "$project/Cargo.toml" \
                -p benchmark_macros -q
        fi
        run_case_prepared "rust_macro_debug_${count}_clean" "macro_compare_rust_clean" \
            "$project/app/src/main.rs" prepare_rust_clean \
            cargo check --release --manifest-path "$project/Cargo.toml" -q
        cargo check --release --manifest-path "$project/Cargo.toml" -q >/dev/null 2>&1
        run_case "rust_macro_debug_${count}_warm" "macro_compare_rust_warm" \
            "$project/app/src/main.rs" \
            cargo check --release --manifest-path "$project/Cargo.toml" -q
        run_case_prepared "rust_macro_debug_${count}_unrelated" \
            "macro_compare_rust_unrelated" "$project/app/src/main.rs" \
            prepare_rust_unrelated_edit \
            cargo check --release --manifest-path "$project/Cargo.toml" -q
        run_case_prepared "rust_macro_debug_${count}_decorated" \
            "macro_compare_rust_decorated" "$project/app/src/main.rs" \
            prepare_rust_decorated_edit \
            cargo check --release --manifest-path "$project/Cargo.toml" -q
        run_case "rust_macro_debug_${count}_handwritten" "macro_compare_rust_handwritten" \
            "$project/app/src/handwritten.rs" \
            rustc --edition=2021 --crate-type=bin "$project/app/src/handwritten.rs" \
            -o "$project/target/handwritten"
    done
}
