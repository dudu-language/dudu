#pragma once

#include "dudu/core/ast.hpp"

#include <vector>

namespace dudu {

bool native_function_equivalent(const NativeFunctionDecl& lhs, const NativeFunctionDecl& rhs);
bool contains_equivalent_native_function(const std::vector<NativeFunctionDecl>& functions,
                                         const NativeFunctionDecl& candidate);
void merge_native_type_declaration(NativeTypeDecl& target, const NativeTypeDecl& source);
void merge_native_class_declaration(ClassDecl& target, const ClassDecl& source);
void append_unique_native_functions(std::vector<NativeFunctionDecl>& target,
                                    const std::vector<NativeFunctionDecl>& source);
void append_unique_native_types(std::vector<NativeTypeDecl>& target,
                                const std::vector<NativeTypeDecl>& source);
void append_unique_native_values(std::vector<NativeValueDecl>& target,
                                 const std::vector<NativeValueDecl>& source);
void append_unique_native_macros(std::vector<NativeMacroDecl>& target,
                                 const std::vector<NativeMacroDecl>& source);
void append_unique_native_namespaces(std::vector<NativeNamespaceDecl>& target,
                                     const std::vector<NativeNamespaceDecl>& source);
void append_unique_native_classes(std::vector<ClassDecl>& target,
                                  const std::vector<ClassDecl>& source);

} // namespace dudu
