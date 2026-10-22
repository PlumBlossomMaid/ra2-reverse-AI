#include "crt_error.h"

namespace ra2::crt {

// 0x7DCDC0：字段写入顺序按原版（+0xC 先清零，最后写 vtable），
// 引用计数分支在字段全部落定之后。
CrtExceptionObject crt_exception_ctor(CrtThrowWorld& world,
                                      std::uint32_t a,
                                      std::uint32_t b,
                                      std::uint32_t c) {
    CrtExceptionObject obj;
    obj.field_c = 0;
    obj.field4 = a;
    obj.field8 = b;
    obj.vtable = kCrtExceptionVtable;

    if (b != 0 && (c & 0xFFu) != 0) {
        world.ReleaseRef(b);
    }
    return obj;
}

// 0x7DCD9C：noreturn——构造完直接进 __CxxThrowException。
void crt_throw_exception(CrtThrowWorld& world, std::uint32_t a,
                         std::uint32_t b) {
    CrtExceptionObject obj = crt_exception_ctor(world, a, b, 0);
    world.CxxThrow(obj, kCrtExceptionThrowInfo);
}

// 0x7DC720：单栈参转发，第二字段写死 0。
void invalid_parameter_forward(CrtThrowWorld& world, std::uint32_t arg) {
    crt_throw_exception(world, arg, 0);
}

}  // namespace ra2::crt