#include "dudu/lsp/language_server_class_members.hpp"

#include "dudu/core/ast_type.hpp"
#include "dudu/lsp/language_server_symbols.hpp"

#include <optional>

namespace dudu {
namespace {

std::optional<Symbol> member_symbol_for_class(const ClassDecl& klass, const std::string& member) {
    for (const FieldDecl& field : klass.fields) {
        if (field.name == member) {
            return Symbol{.name = field.name,
                          .detail = field.name + ": " + type_ref_text(field.type_ref),
                          .location = field.location,
                          .kind = lsp_symbol_kind::Field,
                          .native_identity_key = std::nullopt,
                          .doc_comment = field.doc_comment};
        }
    }
    for (const ConstDecl& constant : klass.constants) {
        if (constant.name == member) {
            return Symbol{.name = constant.name,
                          .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                          .location = constant.location,
                          .kind = lsp_symbol_kind::Constant,
                          .native_identity_key = std::nullopt,
                          .doc_comment = constant.doc_comment};
        }
    }
    for (const ConstDecl& field : klass.static_fields) {
        if (field.name == member) {
            return Symbol{.name = field.name,
                          .detail = field.name + ": " + type_ref_text(field.type_ref),
                          .location = field.location,
                          .kind = lsp_symbol_kind::Field,
                          .native_identity_key = std::nullopt,
                          .doc_comment = field.doc_comment};
        }
    }
    for (const FunctionDecl& method : klass.methods) {
        if (method.name == member) {
            return Symbol{.name = method.name,
                          .detail = function_detail(method),
                          .location = method.location,
                          .kind = is_constructor_method_name(method.name)
                                      ? lsp_symbol_kind::Constructor
                                      : lsp_symbol_kind::Method,
                          .native_identity_key = std::nullopt,
                          .doc_comment = method.doc_comment};
        }
    }
    return std::nullopt;
}

std::vector<Symbol> member_symbols_for_class(const ClassDecl& klass) {
    std::vector<Symbol> out;
    out.reserve(klass.fields.size() + klass.constants.size() + klass.static_fields.size() +
                klass.methods.size());
    for (const FieldDecl& field : klass.fields) {
        out.push_back(Symbol{.name = field.name,
                             .detail = field.name + ": " + type_ref_text(field.type_ref),
                             .location = field.location,
                             .kind = lsp_symbol_kind::Field,
                             .native_identity_key = std::nullopt,
                             .doc_comment = field.doc_comment});
    }
    for (const ConstDecl& constant : klass.constants) {
        out.push_back(Symbol{.name = constant.name,
                             .detail = constant.name + ": " + type_ref_text(constant.type_ref),
                             .location = constant.location,
                             .kind = lsp_symbol_kind::Constant,
                             .native_identity_key = std::nullopt,
                             .doc_comment = constant.doc_comment});
    }
    for (const ConstDecl& field : klass.static_fields) {
        out.push_back(Symbol{.name = field.name,
                             .detail = field.name + ": " + type_ref_text(field.type_ref),
                             .location = field.location,
                             .kind = lsp_symbol_kind::Field,
                             .native_identity_key = std::nullopt,
                             .doc_comment = field.doc_comment});
    }
    for (const FunctionDecl& method : klass.methods) {
        out.push_back(Symbol{.name = method.name,
                             .detail = function_detail(method),
                             .location = method.location,
                             .kind = is_constructor_method_name(method.name)
                                         ? lsp_symbol_kind::Constructor
                                         : lsp_symbol_kind::Method,
                             .native_identity_key = std::nullopt,
                             .doc_comment = method.doc_comment});
    }
    return out;
}

std::optional<Symbol> class_member_symbol_for_owner(const std::vector<ClassDecl>& classes,
                                                    const std::string& owner,
                                                    const std::string& member) {
    for (const ClassDecl& klass : classes) {
        if (klass.name != owner) {
            continue;
        }
        return member_symbol_for_class(klass, member);
    }
    return std::nullopt;
}

std::vector<Symbol> class_member_symbols_for_owner(const std::vector<ClassDecl>& classes,
                                                   const std::string& owner) {
    for (const ClassDecl& klass : classes) {
        if (klass.name == owner) {
            return member_symbols_for_class(klass);
        }
    }
    return {};
}

} // namespace

std::vector<Symbol> class_member_symbols_for_owner(const ModuleAst& module,
                                                   const std::string& owner) {
    std::vector<Symbol> out = class_member_symbols_for_owner(module.classes, owner);
    if (!out.empty()) {
        return out;
    }
    return class_member_symbols_for_owner(module.native_classes, owner);
}

std::optional<Symbol> class_member_symbol_for_path(const ModuleAst& module, const ExprPath& path) {
    if (path.segments.size() != 2 || path.segments[0].kind != ExprPathSegmentKind::Name ||
        path.segments[1].kind != ExprPathSegmentKind::Name) {
        return std::nullopt;
    }
    const std::string& owner = path.segments[0].text;
    const std::string& member = path.segments[1].text;
    if (const std::optional<Symbol> symbol =
            class_member_symbol_for_owner(module.classes, owner, member)) {
        return symbol;
    }
    return class_member_symbol_for_owner(module.native_classes, owner, member);
}

} // namespace dudu
