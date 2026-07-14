#pragma once

#include "dudu/core/source.hpp"
#include "dudu/macro/macro_protocol_generated.hpp"

#include <string>

namespace dudu::macro {

CompileError compile_error_from_macro_diagnostic(const protocol::Diagnostic& diagnostic,
                                                 const protocol::SourceRange& fallback);
CompileError compile_error_from_worker(const protocol::WorkerError& error,
                                       const protocol::SourceRange& invocation,
                                       const std::string& macro_name);

} // namespace dudu::macro
