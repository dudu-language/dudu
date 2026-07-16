#pragma once

#include "dudu/core/ast.hpp"
#include "dudu/native/native_headers.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dudu {

std::string native_header_unquoted(std::string value);
std::optional<std::filesystem::path>
resolve_existing_native_header_path(const ImportDecl& import, const NativeHeaderOptions& options);
std::string native_header_read_text(const std::filesystem::path& path);
void native_header_write_text(const std::filesystem::path& path, const std::string& text);

std::string native_header_scanner_flags(const NativeHeaderOptions& options,
                                        bool include_source_dir = true);
std::vector<std::string> native_header_scanner_arguments(const NativeHeaderOptions& options,
                                                         bool include_source_dir = true);
std::filesystem::path native_header_temp_base(const std::filesystem::path& source_dir);
std::string native_header_compiler_identity(const std::string& command);
std::string native_header_scan_key(std::span<const ImportDecl> imports,
                                   const NativeHeaderOptions& options, const std::string& flags);
std::string native_header_scan_key(const ImportDecl& import, const NativeHeaderOptions& options,
                                   const std::string& flags);

std::string native_header_run_capture(const std::string& command,
                                      const std::filesystem::path& output,
                                      const std::filesystem::path& error);
std::string native_header_scanner_source_for_header(const ImportDecl& import, bool with_c_prelude);
std::string native_header_scanner_source_for_headers(std::span<const ImportDecl> imports,
                                                     bool with_c_prelude);
std::string native_header_clangxx_command();
std::string native_header_clang_base_command(const NativeHeaderOptions& options,
                                             const std::filesystem::path& cpp, bool ast_dump,
                                             const std::string& flags);
std::string native_header_scan_error_message(const ImportDecl& import, std::string detail,
                                             const std::string& clang);

} // namespace dudu
