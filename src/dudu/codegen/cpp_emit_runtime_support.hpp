#pragma once

#include <sstream>

namespace dudu {

void emit_result_runtime_support(std::ostringstream& out);
void emit_print_runtime_support(std::ostringstream& out);
void emit_tuple_runtime_support(std::ostringstream& out);
void emit_index_runtime_support(std::ostringstream& out);
void emit_array_view_runtime_support(std::ostringstream& out);
void emit_strided_span_runtime_support(std::ostringstream& out);
void emit_shader_runtime_support(std::ostringstream& out);

} // namespace dudu
