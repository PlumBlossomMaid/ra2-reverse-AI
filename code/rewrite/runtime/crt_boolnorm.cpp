#include "crt_boolnorm.h"

namespace ra2::crt {

std::int32_t bool_normalize_legacy(CrtBoolNormWorld* world,
                                   std::uintptr_t p) {
    const std::uint32_t r = world->inner(p);  // call 0x7C970C
    // 汇编序列：neg eax / sbb eax,eax / neg eax / dec eax
    //   neg eax → CF = (r != 0)
    //   sbb eax,eax → (r != 0) ? -1 : 0
    //   neg eax → (r != 0) ? 1 : 0
    //   dec eax → (r != 0) ? 0 : -1
    return (r != 0) ? 0 : -1;
}

}  // namespace ra2::crt