#include "dudu/core/ast_type.hpp"
#include "dudu/native/native_header_merge.hpp"
#include "dudu/native/native_header_parse.hpp"

#include <algorithm>

namespace dudu {
namespace {

void apply_summary(std::string& documentation, const NativeDeclarationMetadata& metadata) {
    if (!metadata.summary_doc_comment.empty()) {
        documentation = metadata.summary_doc_comment;
    }
}

void apply_structured_documentation(NativeHeaderScan& scan) {
    for (NativeTypeDecl& type : scan.types) {
        apply_summary(type.doc_comment, type.native_metadata);
    }
    for (NativeValueDecl& value : scan.values) {
        apply_summary(value.doc_comment, value.native_metadata);
    }
    for (NativeFunctionDecl& function : scan.functions) {
        apply_summary(function.doc_comment, function.native_metadata);
    }
    for (ClassDecl& klass : scan.classes) {
        apply_summary(klass.doc_comment, klass.native_metadata);
        for (FunctionDecl& method : klass.methods) {
            apply_summary(method.doc_comment, method.native_metadata);
        }
    }
}

} // namespace

NativeHeaderScan dedupe_scan(NativeHeaderScan scan) {
    apply_structured_documentation(scan);
    NativeHeaderScan out;
    append_unique_native_types(out.types, scan.types);
    append_unique_native_values(out.values, scan.values);

    std::vector<NativeFunctionDecl> valid_functions;
    valid_functions.reserve(scan.functions.size());
    for (NativeFunctionDecl& function : scan.functions) {
        const bool missing_parameter = std::ranges::any_of(
            function.param_type_refs, [](const TypeRef& type) { return !has_type_ref(type); });
        if (has_type_ref(function.return_type_ref) && !missing_parameter) {
            valid_functions.push_back(std::move(function));
        }
    }
    append_unique_native_functions(out.functions, valid_functions);
    append_unique_native_macros(out.macros, scan.macros);
    append_unique_native_namespaces(out.namespaces, scan.namespaces);
    append_unique_native_classes(out.classes, scan.classes);
    return out;
}

} // namespace dudu
