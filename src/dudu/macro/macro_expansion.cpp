#include "dudu/macro/macro_expansion.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/macro/macro_expansion_cache.hpp"
#include "dudu/macro/macro_expansion_internal.hpp"
#include "dudu/macro/macro_registry.hpp"
#include "dudu/macro/macro_runtime_layout.hpp"
#include "dudu/macro/macro_worker_process.hpp"

#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace dudu::macro {
namespace {

namespace p = protocol;

struct PackagePlan {
    std::string key;
    ProjectConfig config;
    Plan plan;
};

class WorkerSessions {
  public:
    p::ExpansionResponse expand(const WorkerBinary& binary,
                                const p::ExpansionRequest& request,
                                std::chrono::milliseconds timeout) {
        std::lock_guard lock(mutex_);
        const std::string key = binary.executable.string();
        auto found = workers_.find(key);
        if (found == workers_.end() || !found->second.running()) {
            WorkerProcess process = WorkerProcess::launch(
                binary.executable, {}, {.request_timeout = timeout});
            const p::MacroCatalog catalog = process.describe();
            if (catalog.binary_identity != binary.identity) {
                throw std::runtime_error("macro worker binary identity mismatch");
            }
            found = workers_.emplace(key, std::move(process)).first;
        }
        return found->second.expand(request);
    }

  private:
    std::mutex mutex_;
    std::map<std::string, WorkerProcess> workers_;
};

WorkerSessions& worker_sessions() {
    static WorkerSessions sessions;
    return sessions;
}

std::filesystem::path package_manifest(const Definition& definition) {
    const std::filesystem::path candidate = find_project_config(definition.location.file.str());
    std::error_code error;
    return std::filesystem::is_regular_file(candidate, error) ? candidate
                                                              : std::filesystem::path{};
}

std::map<std::string, PackagePlan> package_plans(const Plan& complete,
                                                 const ProjectConfig& project) {
    std::map<std::string, PackagePlan> packages;
    for (const auto& [identity, definition] : complete.definitions) {
        const std::filesystem::path manifest = package_manifest(definition);
        const std::string key = manifest.empty()
                                    ? std::filesystem::path(definition.location.file.str())
                                          .parent_path()
                                          .lexically_normal()
                                          .string()
                                    : manifest.lexically_normal().string();
        PackagePlan& package = packages[key];
        if (package.key.empty()) {
            package.key = key;
            package.config = manifest.empty() ? project : parse_project_config(manifest);
        }
        package.plan.definitions.emplace(identity, definition);
    }
    return packages;
}

std::vector<std::string> capabilities(const ProjectConfig& config) {
    std::vector<std::string> out;
    for (const auto& [kind, values] : config.macro_capabilities) {
        if (values.empty()) {
            out.push_back(kind);
        } else {
            for (const std::string& value : values) out.push_back(kind + "=" + value);
        }
    }
    return out;
}

void normalize_non_cacheable_names(WorkerBuildOptions& options, const Plan& plan) {
    std::set<std::string> identities;
    for (const std::string& name : options.non_cacheable_macros) {
        const auto exact = plan.definitions.find(name);
        if (exact != plan.definitions.end()) {
            identities.insert(exact->first);
            continue;
        }
        for (const auto& [identity, definition] : plan.definitions) {
            if (definition.name == name) identities.insert(identity);
        }
    }
    options.non_cacheable_macros = std::move(identities);
}

p::Expression compile_value_expression(const std::string& value) {
    p::Expression expression;
    expression.value = value;
    if (value == "true" || value == "false") {
        expression.kind = p::ExpressionKind::BoolLiteral;
    } else if (value == "None") {
        expression.kind = p::ExpressionKind::NoneLiteral;
    } else if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        expression.kind = p::ExpressionKind::StringLiteral;
        expression.value = value.substr(1, value.size() - 2);
    } else if (value.find_first_of(".eE") != std::string::npos) {
        expression.kind = p::ExpressionKind::FloatLiteral;
    } else {
        expression.kind = p::ExpressionKind::IntLiteral;
    }
    return expression;
}

std::vector<p::AttributeArgument> compile_values(const ModuleAst& module) {
    std::vector<p::AttributeArgument> out;
    out.reserve(module.build_values.size());
    for (const auto& [name, value] : module.build_values) {
        out.push_back({.name = name, .value = compile_value_expression(value), .range = {}});
    }
    return out;
}

SourceRange declaration_range(const p::Declaration& declaration, SourceLocation fallback) {
    if (declaration.class_decl) return from_protocol(declaration.class_decl->range, fallback);
    if (declaration.enum_decl) return from_protocol(declaration.enum_decl->range, fallback);
    if (declaration.function_decl) return from_protocol(declaration.function_decl->range, fallback);
    if (declaration.field_decl) return from_protocol(declaration.field_decl->range, fallback);
    if (declaration.constant_decl) return from_protocol(declaration.constant_decl->range, fallback);
    return {fallback, fallback};
}

const PackagePlan& package_for(const std::map<std::string, PackagePlan>& packages,
                               const Definition& definition) {
    const std::filesystem::path manifest = package_manifest(definition);
    const std::string key = manifest.empty()
                                ? std::filesystem::path(definition.location.file.str())
                                      .parent_path()
                                      .lexically_normal()
                                      .string()
                                : manifest.lexically_normal().string();
    const auto found = packages.find(key);
    if (found == packages.end()) throw std::logic_error("macro package plan is missing");
    return found->second;
}

std::string package_name(const PackagePlan& package) {
    if (!package.config.name.empty()) return package.config.name;
    const std::filesystem::path path(package.key);
    return path.filename().empty() ? "macros" : path.filename().string();
}

void mark_resolved_decorators(ModuleAst& module, const Plan& plan) {
    const auto mark = [&](ModuleAst& unit) {
        for (const Invocation& invocation : plan.invocations) {
            if (invocation.target_module != unit.module_path || invocation.decorator == nullptr)
                continue;
            unit.resolved_macro_decorators.insert(decorator_name(*invocation.decorator));
            for (const HelperAttribute& helper : invocation.helper_attributes) {
                if (helper.decorator != nullptr)
                    unit.resolved_macro_decorators.insert(decorator_name(*helper.decorator));
            }
        }
    };
    if (module.module_units.empty()) {
        mark(module);
    } else {
        for (ModuleAst& unit : module.module_units) mark(unit);
    }
}

} // namespace

ExpansionReport expand_module_macros(ModuleAst& module, const ExpansionOptions& options) {
    const Plan plan = build_plan(module);
    if (plan.invocations.empty()) return {};
    mark_resolved_decorators(module, plan);

    const RuntimeLayout runtime = find_runtime_layout();
    const std::filesystem::path configured_build =
        options.project.build_dir.empty() ? std::filesystem::path("build")
                                          : options.project.build_dir;
    const std::filesystem::path cache_root =
        options.cache_dir.empty() ? project_path(options.project, configured_build) / ".dudu/macros"
                                  : options.cache_dir;
    const auto packages = package_plans(plan, options.project);
    std::map<std::string, WorkerBinary> binaries;
    ExpansionReport report;

    for (const auto& [key, package] : packages) {
        WorkerBuildOptions build = worker_build_options(
            package.config, runtime, cache_root / "workers", package_name(package),
            capabilities(package.config));
        normalize_non_cacheable_names(build, package.plan);
        WorkerBinary binary = build_worker_binary(module, package.plan, build);
        if (binary.cache_hit) ++report.worker_cache_hits;
        report.worker_identities.push_back(binary.identity);
        binaries.emplace(key, std::move(binary));
    }

    std::vector<CollectedExpansion> collected;
    collected.reserve(plan.invocations.size());
    for (const Invocation& invocation : plan.invocations) {
        if (invocation.macro == nullptr || invocation.decorator == nullptr)
            throw std::logic_error("incomplete macro invocation plan");
        const PackagePlan& package = package_for(packages, *invocation.macro);
        const WorkerBinary& binary = binaries.at(package.key);
        p::Declaration declaration = declaration_for_invocation(module, invocation);
        const SourceRange invocation_range = invocation.decorator->expr.range;
        p::ExpansionRequest request{
            .macro_name = invocation.macro->identity,
            .declaration = declaration,
            .invocation = to_protocol(invocation_range),
            .compile_values = compile_values(module)};
        const std::string cache_key = expansion_cache_key(binary.identity, request);
        std::optional<p::ExpansionResponse> response =
            read_expansion_cache(cache_root / "expansions", cache_key);
        if (response) {
            ++report.expansion_cache_hits;
        } else {
            response = worker_sessions().expand(binary, request, options.request_timeout);
            if (response->cacheable && response->external_input_hashes.empty())
                write_expansion_cache(cache_root / "expansions", cache_key, *response);
        }
        ++report.invocations;
        collected.push_back({.macro_name = invocation.macro->name,
                             .macro_identity = invocation.macro->identity,
                             .target_module = invocation.target_module,
                             .target_name = invocation.target_name,
                             .target_kind = invocation.target_kind,
                             .invocation = invocation_range,
                             .definition = {invocation.macro->location,
                                            invocation.macro->location},
                             .source_declaration =
                                 declaration_range(declaration, invocation.decorator->location),
                             .expansion = std::move(response->expansion)});
    }
    merge_expansions(module, plan, collected);
    return report;
}

} // namespace dudu::macro
