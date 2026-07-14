# shellcheck shell=bash

write_csharp_macro_comparison() {
    local project="$1"
    local count="$2"
    mkdir -p "$project/generator" "$project/app"
    cat >"$project/generator/BenchmarkGenerator.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <LangVersion>latest</LangVersion>
    <Nullable>enable</Nullable>
    <IsRoslynComponent>true</IsRoslynComponent>
  </PropertyGroup>
  <ItemGroup>
    <Reference Include="Microsoft.CodeAnalysis" HintPath="$(MSBuildBinPath)/Roslyn/bincore/Microsoft.CodeAnalysis.dll" Private="false" />
    <Reference Include="Microsoft.CodeAnalysis.CSharp" HintPath="$(MSBuildBinPath)/Roslyn/bincore/Microsoft.CodeAnalysis.CSharp.dll" Private="false" />
  </ItemGroup>
</Project>
EOF
    cat >"$project/generator/DebugCountGenerator.cs" <<'EOF'
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace BenchmarkGenerator;

[Generator]
public sealed class DebugCountGenerator : IIncrementalGenerator
{
    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        var classes = context.SyntaxProvider.ForAttributeWithMetadataName(
            "DebugCountAttribute",
            static (node, _) => node is ClassDeclarationSyntax,
            static (syntax, _) => (INamedTypeSymbol)syntax.TargetSymbol);

        context.RegisterSourceOutput(classes, static (output, type) =>
        {
            var fieldCount = type.GetMembers()
                .OfType<IFieldSymbol>()
                .Count(field => !field.IsImplicitlyDeclared);
            output.AddSource(
                $"{type.Name}.DebugCount.g.cs",
                $"partial class {type.Name} {{ public int DebugFieldCount() => {fieldCount}; }}");
        });
    }
}
EOF
    cat >"$project/app/BenchmarkApp.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <LangVersion>latest</LangVersion>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <BaseOutputPath>bin/benchmark/</BaseOutputPath>
    <BaseIntermediateOutputPath>obj/benchmark/</BaseIntermediateOutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="Program.cs" />
    <ProjectReference Include="../generator/BenchmarkGenerator.csproj"
                      OutputItemType="Analyzer"
                      ReferenceOutputAssembly="false" />
  </ItemGroup>
</Project>
EOF
    cat >"$project/app/Handwritten.csproj" <<'EOF'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net8.0</TargetFramework>
    <LangVersion>latest</LangVersion>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <BaseOutputPath>bin/handwritten/</BaseOutputPath>
    <BaseIntermediateOutputPath>obj/handwritten/</BaseIntermediateOutputPath>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="Handwritten.cs" />
  </ItemGroup>
</Project>
EOF
    {
        printf 'using System;\n\n'
        printf '[AttributeUsage(AttributeTargets.Class)]\n'
        printf 'sealed class DebugCountAttribute : Attribute { }\n\n'
        for ((index = 0; index < count; ++index)); do
            printf '[DebugCount]\n'
            printf 'partial class BenchType%d { public int Value; public float Weight; }\n\n' "$index"
        done
        printf 'static class Program {\n'
        printf '    static int Unrelated(int value) => value + 1;\n'
        printf '    static void Main() {\n'
        printf '        var value = new BenchType0 { Value = 1, Weight = 2.0f };\n'
        printf '        Console.WriteLine(value.DebugFieldCount() + Unrelated(value.Value));\n'
        printf '    }\n'
        printf '}\n'
    } >"$project/app/Program.cs"
    {
        printf 'using System;\n\n'
        for ((index = 0; index < count; ++index)); do
            printf 'class BenchType%d { public int Value; public float Weight; public int DebugFieldCount() => 2; }\n\n' "$index"
        done
        printf 'static class Program {\n'
        printf '    static int Unrelated(int value) => value + 1;\n'
        printf '    static void Main() {\n'
        printf '        var value = new BenchType0 { Value = 1, Weight = 2.0f };\n'
        printf '        Console.WriteLine(value.DebugFieldCount() + Unrelated(value.Value));\n'
        printf '    }\n'
        printf '}\n'
    } >"$project/app/Handwritten.cs"
    dotnet restore "$project/app/BenchmarkApp.csproj" -v:q
    dotnet restore "$project/app/Handwritten.csproj" -v:q
}

prepare_csharp_package_clean() {
    rm -rf "$csharp_prepare_project/generator/bin" \
        "$csharp_prepare_project/generator/obj/Release"
}

prepare_csharp_app_clean() {
    prepare_csharp_package_clean
    rm -rf "$csharp_prepare_project/app/bin/benchmark" \
        "$csharp_prepare_project/app/obj/benchmark/Release"
}

prepare_csharp_handwritten_clean() {
    rm -rf "$csharp_prepare_project/app/bin/handwritten" \
        "$csharp_prepare_project/app/obj/handwritten/Release"
}

prepare_csharp_unrelated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/value \+ 1/value + 2/' "$csharp_prepare_project/app/Program.cs"
    else
        perl -0pi -e 's/value \+ 2/value + 1/' "$csharp_prepare_project/app/Program.cs"
    fi
}

prepare_csharp_decorated_edit() {
    local sample="$1"
    if ((sample % 2 == 1)); then
        perl -0pi -e 's/public int Value; public float Weight;/public int Value; public float Weight; public int Extra;/' \
            "$csharp_prepare_project/app/Program.cs"
    else
        perl -0pi -e 's/public int Value; public float Weight; public int Extra;/public int Value; public float Weight;/' \
            "$csharp_prepare_project/app/Program.cs"
    fi
}

run_csharp_macro_comparison() {
    local comparison_root="$macro_bench_root/compare_csharp"
    local count project
    rm -rf "$comparison_root"
    for count in 100 1000; do
        project="$comparison_root/$count"
        write_csharp_macro_comparison "$project" "$count"
        csharp_prepare_project="$project"
        if [[ "$count" -eq 100 ]]; then
            run_case_prepared "csharp_macro_package_clean" "macro_compare_csharp_package" \
                "$project/generator/DebugCountGenerator.cs" prepare_csharp_package_clean \
                dotnet build "$project/generator/BenchmarkGenerator.csproj" -c Release \
                --no-restore -v:q -p:UseSharedCompilation=false
        fi
        run_case_prepared "csharp_macro_debug_${count}_clean" "macro_compare_csharp_clean" \
            "$project/app/Program.cs" prepare_csharp_app_clean \
            dotnet build "$project/app/BenchmarkApp.csproj" -c Release \
            --no-restore -v:q -p:UseSharedCompilation=false
        dotnet build "$project/app/BenchmarkApp.csproj" -c Release \
            --no-restore -v:q -p:UseSharedCompilation=false >/dev/null
        run_case "csharp_macro_debug_${count}_warm" "macro_compare_csharp_warm" \
            "$project/app/Program.cs" dotnet build "$project/app/BenchmarkApp.csproj" \
            -c Release --no-restore -v:q -p:UseSharedCompilation=false
        run_case_prepared "csharp_macro_debug_${count}_unrelated" \
            "macro_compare_csharp_unrelated" "$project/app/Program.cs" \
            prepare_csharp_unrelated_edit dotnet build "$project/app/BenchmarkApp.csproj" \
            -c Release --no-restore -v:q -p:UseSharedCompilation=false
        run_case_prepared "csharp_macro_debug_${count}_decorated" \
            "macro_compare_csharp_decorated" "$project/app/Program.cs" \
            prepare_csharp_decorated_edit dotnet build "$project/app/BenchmarkApp.csproj" \
            -c Release --no-restore -v:q -p:UseSharedCompilation=false
        run_case_prepared "csharp_macro_debug_${count}_handwritten" \
            "macro_compare_csharp_handwritten" "$project/app/Handwritten.cs" \
            prepare_csharp_handwritten_clean dotnet build \
            "$project/app/Handwritten.csproj" -c Release \
            --no-restore -v:q -p:UseSharedCompilation=false
    done
}
