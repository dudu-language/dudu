# shellcheck shell=bash

if ! command -v swift >/dev/null 2>&1 && [[ -f "$HOME/.local/share/swiftly/env.sh" ]]; then
    # shellcheck source=/dev/null
    source "$HOME/.local/share/swiftly/env.sh"
fi

write_swift_macro_comparison() {
    local project="$1"
    local count="$2"
    mkdir -p "$project/Sources/BenchmarkMacrosPlugin" \
        "$project/Sources/BenchmarkMacros" "$project/Sources/BenchmarkApp" \
        "$project/Sources/BenchmarkHandwritten"
    cat >"$project/Package.swift" <<'EOF'
// swift-tools-version: 6.3
import PackageDescription
import CompilerPluginSupport

let package = Package(
    name: "BenchmarkMacros",
    products: [
        .library(name: "BenchmarkMacros", targets: ["BenchmarkMacros"]),
        .executable(name: "BenchmarkApp", targets: ["BenchmarkApp"]),
        .executable(name: "BenchmarkHandwritten", targets: ["BenchmarkHandwritten"]),
    ],
    dependencies: [
        .package(url: "https://github.com/swiftlang/swift-syntax.git", from: "603.0.0-latest"),
    ],
    targets: [
        .macro(
            name: "BenchmarkMacrosPlugin",
            dependencies: [
                .product(name: "SwiftSyntaxMacros", package: "swift-syntax"),
                .product(name: "SwiftCompilerPlugin", package: "swift-syntax"),
            ]
        ),
        .target(name: "BenchmarkMacros", dependencies: ["BenchmarkMacrosPlugin"]),
        .executableTarget(name: "BenchmarkApp", dependencies: ["BenchmarkMacros"]),
        .executableTarget(name: "BenchmarkHandwritten"),
    ],
    swiftLanguageModes: [.v6]
)
EOF
    cat >"$project/Sources/BenchmarkMacrosPlugin/DebugCountMacro.swift" <<'EOF'
import SwiftCompilerPlugin
import SwiftSyntax
import SwiftSyntaxBuilder
import SwiftSyntaxMacros

public struct DebugCountMacro: MemberMacro {
    public static func expansion(
        of node: AttributeSyntax,
        providingMembersOf declaration: some DeclGroupSyntax,
        conformingTo protocols: [TypeSyntax],
        in context: some MacroExpansionContext
    ) throws -> [DeclSyntax] {
        let count = declaration.memberBlock.members.reduce(into: 0) { result, member in
            if member.decl.is(VariableDeclSyntax.self) {
                result += 1
            }
        }
        return [DeclSyntax(stringLiteral: "func debugFieldCount() -> Int32 { Int32(\(count)) }")]
    }
}

@main
struct BenchmarkMacrosPlugin: CompilerPlugin {
    let providingMacros: [Macro.Type] = [DebugCountMacro.self]
}
EOF
    cat >"$project/Sources/BenchmarkMacros/BenchmarkMacros.swift" <<'EOF'
@attached(member, names: named(debugFieldCount))
public macro DebugCount() = #externalMacro(
    module: "BenchmarkMacrosPlugin",
    type: "DebugCountMacro"
)
EOF
    {
        printf 'import BenchmarkMacros\n\n'
        for ((index = 0; index < count; ++index)); do
            printf '@DebugCount\n'
            printf 'struct BenchType%d { var value: Int32 = 0; var weight: Float = 0 }\n\n' "$index"
        done
        printf 'func unrelated(_ value: Int32) -> Int32 { value + 1 }\n\n'
        printf 'let value = BenchType0(value: 1, weight: 2.0)\n'
        printf 'print(value.debugFieldCount() + unrelated(value.value))\n'
    } >"$project/Sources/BenchmarkApp/main.swift"
    {
        for ((index = 0; index < count; ++index)); do
            printf 'struct BenchType%d {\n' "$index"
            printf '    var value: Int32 = 0\n'
            printf '    var weight: Float = 0\n'
            printf '    func debugFieldCount() -> Int32 { 2 }\n'
            printf '}\n\n'
        done
        printf 'func unrelated(_ value: Int32) -> Int32 { value + 1 }\n\n'
        printf 'let value = BenchType0(value: 1, weight: 2.0)\n'
        printf 'print(value.debugFieldCount() + unrelated(value.value))\n'
    } >"$project/Sources/BenchmarkHandwritten/main.swift"
}

swift_remove_target_artifacts() {
    local target="$1"
    rm -rf "$swift_build_path/${target}.build" "$swift_build_path/${target}.product"
    rm -f "$swift_build_path/$target" "$swift_build_path/Modules/${target}.swiftmodule" \
        "$swift_build_path/Modules/${target}.swiftdoc" \
        "$swift_build_path/Modules/${target}.swiftsourceinfo" \
        "$swift_build_path/Modules-tool/${target}.swiftmodule" \
        "$swift_build_path/Modules-tool/${target}.swiftdoc" \
        "$swift_build_path/Modules-tool/${target}.swiftsourceinfo"
}

prepare_swift_package_clean() {
    swift_remove_target_artifacts "BenchmarkMacrosPlugin-tool"
}

prepare_swift_app_clean() {
    prepare_swift_package_clean
    swift_remove_target_artifacts "BenchmarkMacros"
    swift_remove_target_artifacts "BenchmarkApp"
}

prepare_swift_handwritten_clean() {
    swift_remove_target_artifacts "BenchmarkHandwritten"
}

prepare_swift_unrelated_edit() {
    local sample="$1"
    local source="$swift_prepare_project/Sources/BenchmarkApp/main.swift"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/value \+ 1/value + 2/' "$source"
    else
        perl -0pi -e 's/value \+ 2/value + 1/' "$source"
    fi
}

prepare_swift_decorated_edit() {
    local sample="$1"
    local source="$swift_prepare_project/Sources/BenchmarkApp/main.swift"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/struct BenchType0 \{ var value: Int32 = 0; var weight: Float = 0 \}/struct BenchType0 { var value: Int32 = 0; var weight: Float = 0; var extra: Int32 = 0 }/' \
            "$source"
    else
        perl -0pi -e 's/struct BenchType0 \{ var value: Int32 = 0; var weight: Float = 0; var extra: Int32 = 0 \}/struct BenchType0 { var value: Int32 = 0; var weight: Float = 0 }/' \
            "$source"
    fi
}

run_swift_macro_comparison() {
    local project="$bench_dir/macro_compare_swift_workspace"
    local count
    mkdir -p "$project"
    for count in 100 1000; do
        write_swift_macro_comparison "$project" "$count"
        swift_prepare_project="$project"
        swift package resolve --package-path "$project" >/dev/null
        swift build --package-path "$project" -c release \
            --target BenchmarkMacrosPlugin >/dev/null
        swift_build_path="$(swift build --package-path "$project" -c release --show-bin-path)"
        if [[ "$count" -eq 100 ]]; then
            run_case_prepared "swift_macro_package_clean" "macro_compare_swift_package" \
                "$project/Sources/BenchmarkMacrosPlugin/DebugCountMacro.swift" \
                prepare_swift_package_clean swift build --package-path "$project" \
                -c release --target BenchmarkMacrosPlugin
        fi
        run_case_prepared "swift_macro_debug_${count}_clean" "macro_compare_swift_clean" \
            "$project/Sources/BenchmarkApp/main.swift" prepare_swift_app_clean \
            swift build --package-path "$project" -c release --product BenchmarkApp
        swift build --package-path "$project" -c release --product BenchmarkApp >/dev/null
        run_case "swift_macro_debug_${count}_warm" "macro_compare_swift_warm" \
            "$project/Sources/BenchmarkApp/main.swift" swift build \
            --package-path "$project" -c release --product BenchmarkApp
        run_case_prepared "swift_macro_debug_${count}_unrelated" \
            "macro_compare_swift_unrelated" "$project/Sources/BenchmarkApp/main.swift" \
            prepare_swift_unrelated_edit swift build --package-path "$project" \
            -c release --product BenchmarkApp
        run_case_prepared "swift_macro_debug_${count}_decorated" \
            "macro_compare_swift_decorated" "$project/Sources/BenchmarkApp/main.swift" \
            prepare_swift_decorated_edit swift build --package-path "$project" \
            -c release --product BenchmarkApp
        run_case_prepared "swift_macro_debug_${count}_handwritten" \
            "macro_compare_swift_handwritten" \
            "$project/Sources/BenchmarkHandwritten/main.swift" \
            prepare_swift_handwritten_clean swift build --package-path "$project" \
            -c release --product BenchmarkHandwritten
    done
}
