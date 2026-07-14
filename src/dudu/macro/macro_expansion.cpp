#include "dudu/macro/macro_expansion.hpp"

#include "dudu/core/ast_expr.hpp"
#include "dudu/core/decorators.hpp"
#include "dudu/macro/macro_ast_bridge.hpp"
#include "dudu/macro/macro_diagnostic_bridge.hpp"
#include "dudu/macro/macro_expansion_cache.hpp"
#include "dudu/macro/macro_expansion_internal.hpp"
#include "dudu/macro/macro_hygiene.hpp"
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

using Clock = std::chrono::steady_clock;

std::uint64_t elapsed_ns(Clock::time_point start) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
}

struct PackagePlan {
    std::string key;
    ProjectConfig config;
    Plan plan;
};

class WorkerSessions {
  public:
    struct Result {
        p::ExpansionResponse response;
        bool started = false;
        std::uint64_t start_ns = 0;
        std::uint64_t protocol_ns = 0;
        std::uint64_t validate_ns = 0;
    };

    Result expand(const std::string& session_key, const WorkerBinary& binary,
                  const p::ExpansionRequest& request, std::chrono::milliseconds timeout) {
        std::lock_guard lock(mutex_);
        Result result;
        auto found = workers_.find(session_key);
        if (found == workers_.end() || found->second.identity != binary.identity ||
            !found->second.process.running()) {
            const Clock::time_point start = Clock::now();
            WorkerProcess process = WorkerProcess::launch(
                binary.executable, {},
                {.request_timeout = timeout, .working_directory = binary.working_directory});
            const p::MacroCatalog catalog = process.describe();
            if (catalog.binary_identity != binary.identity) {
                throw std::runtime_error("macro worker binary identity mismatch");
            }
            Session session{.identity = binary.identity, .process = std::move(process)};
            if (found == workers_.end()) {
                found = workers_.emplace(session_key, std::move(session)).first;
            } else {
                found->second = std::move(session);
            }
            result.started = true;
            result.start_ns = elapsed_ns(start);
        }
        WorkerExpansionResult measured = found->second.process.expand_measured(request);
        result.response = std::move(measured.response);
        result.validate_ns = measured.response_decode_ns;
        const std::uint64_t worker_transport_ns =
            measured.transport_ns > result.response.execute_ns
                ? measured.transport_ns - result.response.execute_ns
                : 0;
        result.protocol_ns = measured.request_encode_ns + worker_transport_ns;
        return result;
    }

    std::size_t resident_set_kb(const std::string& session_key) {
        std::lock_guard lock(mutex_);
        const auto found = workers_.find(session_key);
        if (found == workers_.end() || !found->second.process.running())
            return 0;
        return found->second.process.resident_set_kb().value_or(0);
    }

  private:
    struct Session {
        std::string identity;
        WorkerProcess process;
    };

    std::mutex mutex_;
    std::map<std::string, Session> workers_;
};

WorkerSessions& worker_sessions() {
    static WorkerSessions sessions;
    return sessions;
}

std::filesystem::path package_manifest(const Definition& definition) {
    const std::filesystem::path candidate = find_project_config(definition.location.file.str());
    std::error_code error;
    return std::filesystem::is_regular_file(candidate, error) ? candidate : std::filesystem::path{};
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
            for (const std::string& value : values)
                out.push_back(kind + "=" + value);
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
            if (definition.name == name)
                identities.insert(identity);
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
    if (declaration.class_decl)
        return from_protocol(declaration.class_decl->range, fallback);
    if (declaration.enum_decl)
        return from_protocol(declaration.enum_decl->range, fallback);
    if (declaration.function_decl)
        return from_protocol(declaration.function_decl->range, fallback);
    if (declaration.field_decl)
        return from_protocol(declaration.field_decl->range, fallback);
    if (declaration.constant_decl)
        return from_protocol(declaration.constant_decl->range, fallback);
    return {fallback, fallback};
}

std::string package_name(const PackagePlan& package) {
    if (!package.config.name.empty())
        return package.config.name;
    const std::filesystem::path path(package.key);
    return path.filename().empty() ? "macros" : path.filename().string();
}

void mark_resolved_decorators(ModuleAst& module, const Plan& plan) {
    const auto mark = [&](ModuleAst& unit) {
        for (const Invocation& invocation : plan.invocations) {
            if (invocation.target_module != unit.module_path || invocation.decorator == nullptr)
                continue;
            unit.resolved_macro_decorators.insert(decorator_name(*invocation.decorator));
            if (invocation.macro != nullptr) {
                unit.resolved_macro_decorators.insert(invocation.macro->name);
                unit.resolved_macro_decorators.insert(invocation.macro->identity);
            }
            for (const HelperAttribute& helper : invocation.helper_attributes) {
                if (helper.decorator != nullptr)
                    unit.resolved_macro_decorators.insert(decorator_name(*helper.decorator));
            }
        }
    };
    if (module.module_units.empty()) {
        mark(module);
    } else {
        for (ModuleAst& unit : module.module_units)
            mark(unit);
    }
}

void mark_macro_host_modules(ModuleAst& module, const Plan& plan) {
    std::set<std::string> host_modules;
    for (const auto& [_, definition] : plan.definitions) {
        host_modules.insert(definition.module_path);
    }
    if (!plan.definitions.empty()) {
        host_modules.insert("dudu.ast");
        host_modules.insert("dudu.macro");
    }
    if (module.module_units.empty()) {
        if (host_modules.contains(module.module_path)) {
            module.compilation_domain = CompilationDomain::MacroHost;
        }
        return;
    }
    for (ModuleAst& unit : module.module_units) {
        if (host_modules.contains(unit.module_path)) {
            unit.compilation_domain = CompilationDomain::MacroHost;
        }
    }
}

} // namespace

ExpansionReport expand_module_macros(ModuleAst& module, const ExpansionOptions& options) {
    const Clock::time_point plan_start = Clock::now();
    const Plan plan = build_plan(module);
    mark_macro_host_modules(module, plan);
    ExpansionReport report;
    report.timings.plan_ns = elapsed_ns(plan_start);
    for (const auto& [_, definition] : plan.definitions) {
        report.definitions.push_back(
            {.name = definition.name,
             .identity = definition.identity,
             .module_path = definition.module_path,
             .accepted_kind = std::string(target_kind_name(definition.accepted_kind)),
             .documentation =
                 definition.function == nullptr ? "" : definition.function->doc_comment,
             .location = to_protocol(definition.location),
             .attribute_schema =
                 definition.attribute_schema == nullptr
                     ? std::optional<p::ClassDecl>{}
                     : to_protocol(*definition.attribute_schema, definition.module_path)});
    }
    if (plan.invocations.empty())
        return report;
    const Clock::time_point setup_start = Clock::now();
    mark_resolved_decorators(module, plan);

    const RuntimeLayout runtime = find_runtime_layout();
    const std::filesystem::path configured_build = options.project.build_dir.empty()
                                                       ? std::filesystem::path("build")
                                                       : options.project.build_dir;
    const std::filesystem::path cache_root =
        options.cache_dir.empty() ? project_path(options.project, configured_build) / ".dudu/macros"
                                  : options.cache_dir;
    const auto packages = package_plans(plan, options.project);
    report.timings.setup_ns = elapsed_ns(setup_start);
    std::map<std::string, WorkerBinary> binaries;
    std::map<std::string, const PackagePlan*> packages_by_macro;
    for (const auto& [key, package] : packages) {
        for (const auto& [identity, _] : package.plan.definitions)
            packages_by_macro.emplace(identity, &package);
        WorkerBuildOptions build =
            worker_build_options(package.config, runtime, cache_root / "workers",
                                 package_name(package), capabilities(package.config));
        normalize_non_cacheable_names(build, package.plan);
        const Clock::time_point package_start = Clock::now();
        WorkerBinary binary = build_worker_binary(module, package.plan, build);
        report.timings.package_build_ns += elapsed_ns(package_start);
        if (binary.cache_hit)
            ++report.worker_cache_hits;
        report.worker_identities.push_back(binary.identity);
        binaries.emplace(key, std::move(binary));
    }

    std::vector<CollectedExpansion> collected;
    collected.reserve(plan.invocations.size());
    const Clock::time_point declaration_bridge_start = Clock::now();
    std::vector<p::Declaration> declarations =
        declarations_for_invocations(module, plan.invocations);
    report.timings.declaration_bridge_ns = elapsed_ns(declaration_bridge_start);
    const Clock::time_point request_loop_start = Clock::now();
    for (std::size_t invocation_index = 0; invocation_index < plan.invocations.size();
         ++invocation_index) {
        const Invocation& invocation = plan.invocations[invocation_index];
        if (invocation.macro == nullptr || invocation.decorator == nullptr)
            throw std::logic_error("incomplete macro invocation plan");
        const auto found_package = packages_by_macro.find(invocation.macro->identity);
        if (found_package == packages_by_macro.end())
            throw std::logic_error("macro package plan is missing");
        const PackagePlan& package = *found_package->second;
        const WorkerBinary& binary = binaries.at(package.key);
        p::Declaration& declaration = declarations[invocation_index];
        const SourceRange invocation_range = invocation.decorator->expr.range;
        p::ExpansionRequest request{.macro_name = invocation.macro->identity,
                                    .declaration = declaration,
                                    .invocation = to_protocol(invocation_range),
                                    .compile_values = compile_values(module)};
        const Clock::time_point cache_key_start = Clock::now();
        const std::string cache_key = expansion_cache_key(binary.identity, request);
        report.timings.cache_key_ns += elapsed_ns(cache_key_start);
        const Clock::time_point cache_start = Clock::now();
        std::optional<p::ExpansionResponse> response =
            read_expansion_cache(cache_root / "expansions", cache_key);
        report.timings.cache_read_ns += elapsed_ns(cache_start);
        if (response) {
            ++report.expansion_cache_hits;
        } else {
            try {
                WorkerSessions::Result execution =
                    worker_sessions().expand(package.key, binary, request, options.request_timeout);
                response = std::move(execution.response);
                ++report.worker_executions;
                if (execution.started) {
                    ++report.worker_starts;
                    report.timings.worker_start_ns += execution.start_ns;
                }
                report.timings.execute_ns += response->execute_ns;
                report.timings.protocol_ns += execution.protocol_ns;
                report.timings.validate_ns += execution.validate_ns;
            } catch (const WorkerProcessError& error) {
                throw compile_error_from_worker(error.detail(), request.invocation,
                                                invocation.macro->name);
            } catch (const std::exception& error) {
                const p::WorkerError detail{
                    .code = "dudu.macro.worker", .message = error.what(), .diagnostics = {}};
                throw compile_error_from_worker(detail, request.invocation, invocation.macro->name);
            }
            if (response->cacheable) {
                const Clock::time_point cache_write_start = Clock::now();
                write_expansion_cache(cache_root / "expansions", cache_key, *response);
                report.timings.cache_write_ns += elapsed_ns(cache_write_start);
            }
        }
        const Clock::time_point collect_start = Clock::now();
        ++report.invocations;
        report.generated_nodes += p::count_nodes(response->expansion);
        report.expansions.push_back(
            {.macro_name = invocation.macro->name,
             .macro_identity = invocation.macro->identity,
             .target_module = invocation.target_module,
             .target_name = invocation.target_name,
             .invocation = request.invocation,
             .definition =
                 to_protocol(SourceRange{invocation.macro->location, invocation.macro->location}),
             .source_declaration =
                 to_protocol(declaration_range(declaration, invocation.decorator->location)),
             .expansion = response->expansion});
        p::Expansion merge_expansion = response->expansion;
        const Clock::time_point hygiene_start = Clock::now();
        apply_expansion_hygiene(merge_expansion, invocation.macro->identity,
                                invocation.target_module, invocation.target_name,
                                request.invocation);
        report.timings.hygiene_ns += elapsed_ns(hygiene_start);
        collected.push_back(
            {.macro_name = invocation.macro->name,
             .macro_identity = invocation.macro->identity,
             .target_module = invocation.target_module,
             .target_name = invocation.target_name,
             .target_kind = invocation.target_kind,
             .invocation = invocation_range,
             .definition = {invocation.macro->location, invocation.macro->location},
             .source_declaration = declaration_range(declaration, invocation.decorator->location),
             .expansion = std::move(merge_expansion)});
        report.timings.collect_ns += elapsed_ns(collect_start);
    }
    report.timings.request_loop_ns = elapsed_ns(request_loop_start);
    if (report.worker_executions != 0) {
        for (const auto& [key, _] : packages) {
            report.worker_rss_kb =
                std::max(report.worker_rss_kb, worker_sessions().resident_set_kb(key));
        }
    }
    const Clock::time_point merge_start = Clock::now();
    merge_expansions(module, plan, collected);
    report.timings.merge_ns += elapsed_ns(merge_start);
    return report;
}

} // namespace dudu::macro
