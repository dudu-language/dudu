#pragma once

#include "dudu/core/ast.hpp"

namespace dudu {

void merge_native_declaration_metadata(NativeDeclarationMetadata& target,
                                       const NativeDeclarationMetadata& source);
void merge_native_value_declaration(NativeValueDecl& target,
                                    const NativeValueDecl& source);
void merge_native_function_declaration(NativeFunctionDecl& target,
                                       const NativeFunctionDecl& source);
void merge_native_macro_declaration(NativeMacroDecl& target,
                                    const NativeMacroDecl& source);
void merge_native_namespace_declaration(NativeNamespaceDecl& target,
                                        const NativeNamespaceDecl& source);
void merge_native_class_members(ClassDecl& target, const ClassDecl& source);

} // namespace dudu
