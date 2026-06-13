#include "dudu/sema_constructors.hpp"

#include "dudu/sema_context.hpp"
#include "dudu/source.hpp"

#include <set>

namespace dudu {
namespace {

[[noreturn]] void fail(const SourceLocation& location, const std::string& message) {
    throw CompileError(location, message);
}

} // namespace

std::vector<ConstructorParam> constructor_params(const ClassDecl& klass) {
    for (const FunctionDecl& method : klass.methods) {
        if (method.name != "__init__") {
            continue;
        }
        std::vector<ConstructorParam> out;
        const size_t first_param =
            !method.params.empty() && method.params.front().name == "self" ? 1 : 0;
        for (size_t i = first_param; i < method.params.size(); ++i) {
            out.push_back({.name = method.params[i].name, .type = method.params[i].type});
        }
        return out;
    }

    std::vector<ConstructorParam> out;
    for (const FieldDecl& field : klass.fields) {
        out.push_back({.name = field.name, .type = field.type});
    }
    return out;
}

void check_constructor_args(
    const FunctionScope& scope, const ClassDecl& klass, const std::vector<std::string>& args,
    const SourceLocation* location,
    const std::function<std::string(const FunctionScope&, std::string, const SourceLocation*)>&
        infer_expr,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>&
        can_assign) {
    if (location == nullptr)
        return;
    const std::vector<ConstructorParam> expected = constructor_params(klass);
    std::set<std::string> named_fields;
    size_t positional = 0;
    for (const std::string& arg : args) {
        const size_t equal = find_top_level_char(arg, '=');
        if (equal == std::string::npos) {
            if (!named_fields.empty())
                fail(*location, "positional constructor argument after named fields: " + klass.name);
            if (positional >= expected.size())
                fail(*location, "constructor " + klass.name + " expects at most " +
                                    std::to_string(expected.size()) + " arguments, got " +
                                    std::to_string(args.size()));
            const ConstructorParam& param = expected[positional];
            const std::string got = infer_expr(scope, arg, location);
            if (!can_assign(param.type, arg, got))
                fail(*location, "constructor " + klass.name + " argument " +
                                    std::to_string(positional + 1) + " expects " + param.type +
                                    ", got " + got);
            ++positional;
            continue;
        }
        const std::string name = trim(arg.substr(0, equal));
        if (!named_fields.insert(name).second)
            fail(*location, "duplicate constructor field: " + name);
        const ConstructorParam* param = nullptr;
        for (const ConstructorParam& candidate : expected)
            if (candidate.name == name)
                param = &candidate;
        if (param == nullptr)
            fail(*location, "unknown constructor field: " + klass.name + "." + name);
        const std::string value = arg.substr(equal + 1);
        const std::string got = infer_expr(scope, value, location);
        if (!can_assign(param->type, value, got))
            fail(*location, "constructor field " + klass.name + "." + name + " expects " +
                                param->type + ", got " + got);
    }
}

} // namespace dudu
