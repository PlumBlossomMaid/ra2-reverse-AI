#pragma once

#include <cstdint>

namespace ra2::runtime {

// 0x7DC720 → 0x7DCD9C → 0x7DCDC0：MSVC6 CRT 无效参数异常链。
//
// 原版机器码（2026-10-19 全段复核 + golden 12 用例实证，
// memory/data/rmg/deadcode_7dc720_golden.txt）：
//
//   0x7DC720（14B，131 unique_callers / 4928 transitive_dependents，
//   ranking.tsv 扇入度最高的未复刻 CRT 项）：
//     push 0                ; ctor arg2 = 0
//     push [esp+8]          ; = 调用方 [esp+4] 参数
//     call 0x7DCD9C         ; InvalidParameterWrapper(arg) ≡ Throw(arg, 0)
//     ret 4                 ; 单栈参
//
//   0x7DCD9C（36B，noreturn——无 ret，尾部即 __CxxThrowException）：
//     push ebp / mov ebp,esp / sub esp,0x10
//     push 0                ; ctor arg2 = 0
//     lea ecx,[ebp-0x10]    ; ctor this = 栈上临时对象
//     push [ebp+0xc]        ; ctor arg1 = b
//     push [ebp+8]          ; ctor arg0 = a
//     call 0x7DCDC0         ; 构造异常对象
//     lea eax,[ebp-0x10]
//     push 0x80F0D0         ; ThrowInfo (RTTI)
//     push eax
//     call 0x7D2F64         ; __CxxThrowException
//
//   0x7DCDC0（48B，ret 0xc——3 个栈参）：
//     eax = [esp+4]（a）；this = ecx
//     [this+0xC] = 0（and dword [esi+0xc],0）
//     [this+4] = a
//     eax = [esp+0xC]（b）；[this] = 0x7FA498（vtable）
//     [this+8] = b
//     b == 0 → 跳过；否则 cmp byte [esp+0x10],0（c 低字节）
//       c == 0 → 跳过；否则：
//         ecx = [b]（b 的 vtable）；push b；call [ecx+4]（引用计数虚调用）
//     eax = this；ret 0xc
//
// 输出对象布局：{vtable=0x7FA498, +4=a, +8=b, +0xC=0}。
// ★原版怪癖（golden 实证，照抄）：
//   ① 先写 +0xC=0 再写其余字段——+0xC 恒 0（wrapper 路径 b=0 不触发
//     引用计数）；
//   ② 0x7DCD9C 无 ret（noreturn）——构造完直接进 __CxxThrowException；
//   ③ wrapper 单栈参 ret 4，第二字段写死 0；
//   ④ ctor 的引用计数分支由**第三个字节参**（c 低字节非 0）且 b 非 0
//     共同门控（先 test b 再 cmp byte c），命中时调 b->vtbl[1](b)。

struct CrtExceptionObject {
    std::uint32_t vtable = 0;   // +0 = 0x7FA498
    std::uint32_t field4 = 0;   // +4 = a
    std::uint32_t field8 = 0;   // +8 = b
    std::uint32_t field_c = 0;  // +0xC = 0（恒）
};

// 这一阶段要问外界的事（原版侧：__CxxThrowException 0x7D2F64 + b->vtbl[1]
// 引用计数虚调用；C++ 侧抽象成世界回调以便对拍计数）。
class CrtThrowWorld {
 public:
    virtual ~CrtThrowWorld() {}

    // 0x7D2F64 __CxxThrowException(obj, throw_info)
    virtual void CxxThrow(const CrtExceptionObject& obj,
                          std::uint32_t throw_info) = 0;

    // b->vtable[1](b)：b 非 0 且 c 低字节非 0 时的引用计数虚调用
    virtual void ReleaseRef(std::uint32_t b) = 0;
};

// 0x7DCDC0：构造异常对象（返回四字段）。b 非 0 且 c 低字节非 0 时调
// ReleaseRef(b)。
CrtExceptionObject crt_exception_ctor(CrtThrowWorld& world,
                                      std::uint32_t a,
                                      std::uint32_t b,
                                      std::uint32_t c);

// 0x7DCD9C：ThrowException(a, b) —— 构造 {vtbl,a,b,0} 后 CxxThrow
// （c 恒 0，不触发引用计数）。
void crt_throw_exception(CrtThrowWorld& world, std::uint32_t a,
                         std::uint32_t b);

// 0x7DC720：InvalidParameterWrapper(arg) ≡ crt_throw_exception(arg, 0)。
void invalid_parameter_forward(CrtThrowWorld& world, std::uint32_t arg);

// 常量（原版硬编码，golden 实证）
inline constexpr std::uint32_t kCrtExceptionVtable = 0x7FA498;
inline constexpr std::uint32_t kCrtExceptionThrowInfo = 0x80F0D0;

}  // namespace ra2::runtime