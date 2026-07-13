#include "dudu/macro/macro_capabilities.hpp"

#include "dudu/macro/macro_hash.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string_view>

namespace dudu::macro {
namespace {

struct Scope {
    std::filesystem::path root;
    std::vector<protocol::Capability> capabilities;
    CapabilityOutcome outcome;
};

thread_local std::optional<Scope> active_scope;

std::string normalized(std::filesystem::path path) {
    return path.lexically_normal().generic_string();
}

bool glob_matches(std::string_view pattern, std::string_view value, std::size_t p = 0,
                  std::size_t v = 0) {
    while (p < pattern.size()) {
        if (pattern[p] == '*') {
            const bool recursive = p + 1 < pattern.size() && pattern[p + 1] == '*';
            p += recursive ? 2 : 1;
            while (p < pattern.size() && pattern[p] == '*')
                ++p;
            if (p == pattern.size()) {
                return recursive || value.find('/', v) == std::string_view::npos;
            }
            for (std::size_t end = v; end <= value.size(); ++end) {
                if (!recursive && end > v && value[end - 1] == '/')
                    break;
                if (glob_matches(pattern, value, p, end))
                    return true;
            }
            return false;
        }
        if (v == value.size())
            return false;
        if (pattern[p] != '?' && pattern[p] != value[v])
            return false;
        ++p;
        ++v;
    }
    return v == value.size();
}

Scope& scope() {
    if (!active_scope) {
        throw std::runtime_error("macro capability API used outside an expansion request");
    }
    return *active_scope;
}

const protocol::Capability* capability(protocol::CapabilityKind kind) {
    Scope& current = scope();
    for (const protocol::Capability& item : current.capabilities) {
        if (item.kind == kind)
            return &item;
    }
    return nullptr;
}

std::string capability_name(protocol::CapabilityKind kind) {
    switch (kind) {
    case protocol::CapabilityKind::FsRead:
        return "fs.read";
    case protocol::CapabilityKind::FsWrite:
        return "fs.write";
    case protocol::CapabilityKind::EnvRead:
        return "env.read";
    case protocol::CapabilityKind::Process:
        return "process";
    case protocol::CapabilityKind::Network:
        return "network";
    case protocol::CapabilityKind::Clock:
        return "clock";
    case protocol::CapabilityKind::Random:
        return "random";
    }
    return "unknown";
}

void require_unscoped(protocol::CapabilityKind kind) {
    if (capability(kind) == nullptr) {
        throw std::runtime_error("macro requires undeclared capability '" + capability_name(kind) +
                                 "'");
    }
}

void require_value(protocol::CapabilityKind kind, std::string_view value) {
    const protocol::Capability* allowed = capability(kind);
    if (allowed == nullptr) {
        throw std::runtime_error("macro requires undeclared capability '" + capability_name(kind) +
                                 "'");
    }
    for (const std::string& pattern : allowed->values) {
        if (glob_matches(pattern, value))
            return;
    }
    throw std::runtime_error("macro capability '" + capability_name(kind) + "' does not allow '" +
                             std::string(value) + "'");
}

std::filesystem::path checked_path(protocol::CapabilityKind kind, const std::string& value,
                                   bool must_exist) {
    Scope& current = scope();
    std::filesystem::path candidate(value);
    if (candidate.is_relative())
        candidate = current.root / candidate;
    std::error_code error;
    std::filesystem::path resolved;
    if (must_exist) {
        resolved = std::filesystem::canonical(candidate, error);
    } else {
        const std::filesystem::path parent =
            std::filesystem::weakly_canonical(candidate.parent_path(), error);
        if (!error)
            resolved = parent / candidate.filename();
    }
    if (error || resolved.empty()) {
        throw std::runtime_error("macro cannot resolve path '" + value + "'");
    }
    std::error_code relative_error;
    const std::filesystem::path relative =
        std::filesystem::relative(resolved, current.root, relative_error);
    const std::string match_value =
        !relative_error && !relative.empty() && *relative.begin() != ".." ? normalized(relative)
                                                                          : normalized(resolved);
    require_value(kind, match_value);
    return resolved;
}

std::string digest(std::string_view value) {
    StableHash hash;
    hash.add_bytes(value);
    return hash.finish();
}

std::string record(std::string_view kind, std::string_view name, std::string_view hash) {
    std::string out(kind);
    out.push_back('\0');
    out.append(name);
    out.push_back('\0');
    out.append(hash);
    return out;
}

struct ParsedRecord {
    std::string_view kind;
    std::string_view name;
    std::string_view hash;
};

std::optional<ParsedRecord> parse_record(std::string_view value) {
    const std::size_t first = value.find('\0');
    if (first == std::string_view::npos)
        return std::nullopt;
    const std::size_t second = value.find('\0', first + 1);
    if (second == std::string_view::npos)
        return std::nullopt;
    return ParsedRecord{value.substr(0, first), value.substr(first + 1, second - first - 1),
                        value.substr(second + 1)};
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("macro cannot read file '" + path.string() + "'");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string command_name(std::string_view command) {
    const std::size_t first = command.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {};
    const std::size_t last = command.find_first_of(" \t", first);
    return std::string(command.substr(first, last - first));
}

} // namespace

void begin_capability_scope(const std::vector<protocol::Capability>& capabilities,
                            const std::filesystem::path& project_root) {
    if (active_scope)
        throw std::logic_error("nested macro capability scope");
    std::error_code error;
    std::filesystem::path root = std::filesystem::weakly_canonical(project_root, error);
    if (error)
        root = project_root.lexically_normal();
    active_scope = Scope{.root = std::move(root), .capabilities = capabilities};
}

CapabilityOutcome finish_capability_scope() {
    CapabilityOutcome result = std::move(scope().outcome);
    active_scope.reset();
    return result;
}

void discard_capability_scope() noexcept {
    active_scope.reset();
}

bool external_inputs_are_current(const std::vector<std::string>& records) {
    for (const std::string& encoded : records) {
        const std::optional<ParsedRecord> item = parse_record(encoded);
        if (!item)
            return false;
        if (item->kind == "fs.read") {
            try {
                if (digest(read_file(std::filesystem::path(item->name))) != item->hash)
                    return false;
            } catch (const std::exception&) {
                return false;
            }
        } else if (item->kind == "env.read") {
            const char* value = std::getenv(std::string(item->name).c_str());
            if (value == nullptr || digest(value) != item->hash)
                return false;
        } else {
            return false;
        }
    }
    return true;
}

namespace host {

std::string read_text(const std::string& path) {
    const std::filesystem::path resolved =
        checked_path(protocol::CapabilityKind::FsRead, path, true);
    std::string contents = read_file(resolved);
    scope().outcome.external_input_hashes.push_back(
        record("fs.read", normalized(resolved), digest(contents)));
    return contents;
}

void write_text(const std::string& path, const std::string& contents) {
    const std::filesystem::path resolved =
        checked_path(protocol::CapabilityKind::FsWrite, path, false);
    std::filesystem::create_directories(resolved.parent_path());
    std::ofstream output(resolved, std::ios::binary | std::ios::trunc);
    if (!output)
        throw std::runtime_error("macro cannot write file '" + resolved.string() + "'");
    output << contents;
    if (!output)
        throw std::runtime_error("macro failed writing file '" + resolved.string() + "'");
    scope().outcome.deterministic = false;
}

std::string read_env(const std::string& name) {
    require_value(protocol::CapabilityKind::EnvRead, name);
    const char* value = std::getenv(name.c_str());
    if (value == nullptr)
        throw std::runtime_error("macro environment value is unset: " + name);
    scope().outcome.external_input_hashes.push_back(record("env.read", name, digest(value)));
    return value;
}

int run(const std::string& command) {
    const std::string executable = command_name(command);
    if (executable.empty())
        throw std::runtime_error("macro process command is empty");
    require_value(protocol::CapabilityKind::Process, executable);
    scope().outcome.deterministic = false;
    return std::system(command.c_str());
}

void require_network(const std::string& endpoint) {
    require_value(protocol::CapabilityKind::Network, endpoint);
    scope().outcome.deterministic = false;
}

std::uint64_t clock_ns() {
    require_unscoped(protocol::CapabilityKind::Clock);
    scope().outcome.deterministic = false;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

std::uint64_t random_u64() {
    require_unscoped(protocol::CapabilityKind::Random);
    scope().outcome.deterministic = false;
    std::random_device source;
    return (static_cast<std::uint64_t>(source()) << 32U) ^ source();
}

} // namespace host
} // namespace dudu::macro
