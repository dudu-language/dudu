#pragma once

#include "dudu/sema_body.hpp"

#include <string>

namespace dudu {

std::string assignment_target_type(FunctionScope& scope, const Stmt& stmt,
                                   const BodyCheckCallbacks& callbacks);

} // namespace dudu
