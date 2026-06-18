#pragma once

#include "dudu/sema_body.hpp"

namespace dudu {

TypeRef assignment_target_type_ref(FunctionScope& scope, const Stmt& stmt,
                                   const BodyCheckCallbacks& callbacks);

} // namespace dudu
