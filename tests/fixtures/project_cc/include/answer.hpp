#pragma once

#ifndef DUDU_PROJECT_CC
#error "DUDU_PROJECT_CC was not defined"
#endif

#ifndef DUDU_PROJECT_CC_FLAG
#error "DUDU_PROJECT_CC_FLAG was not defined"
#endif

namespace answer {
inline int value() {
    return DUDU_PROJECT_CC + DUDU_PROJECT_CC_FLAG;
}
} // namespace answer
