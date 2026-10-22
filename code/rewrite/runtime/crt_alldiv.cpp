#include "crt_alldiv.h"

#include <cstdint>

namespace ra2::crt {

std::int64_t alldiv_legacy(std::int64_t a, std::int64_t b) {
    // 栈参布局（cdecl 右→左压栈）：(a_lo, a_hi, b_lo, b_hi)
    const std::uint32_t a_lo = static_cast<std::uint32_t>(a);
    const std::uint32_t a_hi = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(a) >> 32);
    const std::uint32_t b_lo = static_cast<std::uint32_t>(b);
    const std::uint32_t b_hi = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(b) >> 32);

    // ★符号归一化（edi 计数负号）：
    //   mov eax,[esp+0x14]=a_hi → or eax,eax → jge 跳过（a_hi>=0）
    //   inc edi → neg eax / neg edx / sbb eax,0 = 64 位取负 a = -a
    //   （MSVC6 惯用：neg 低 32 清 CF、neg 高 32、sbb 借位——等价于
    //   (~v)+1 补码回绕；INT64_MIN 取负回绕为自身，照抄）
    std::uint32_t edi = 0;
    std::uint32_t aa_hi = a_hi;
    std::uint32_t aa_lo = a_lo;
    if (static_cast<std::int32_t>(aa_hi) < 0) {  // 符号位判定（jge 的反）
        ++edi;
        const std::uint64_t na = (~static_cast<std::uint64_t>(a)) + 1;
        aa_hi = static_cast<std::uint32_t>(na >> 32);
        aa_lo = static_cast<std::uint32_t>(na);
    }
    std::uint32_t bb_hi = b_hi;
    std::uint32_t bb_lo = b_lo;
    if (static_cast<std::int32_t>(bb_hi) < 0) {  // b 同款
        ++edi;
        const std::uint64_t nb = (~static_cast<std::uint64_t>(b)) + 1;
        bb_hi = static_cast<std::uint32_t>(nb >> 32);
        bb_lo = static_cast<std::uint32_t>(nb);
    }

    std::uint64_t q = 0;

    if (bb_hi == 0) {
        // 快速路径（0x7C9311，b_hi==0——绝对值化后）：
        //   mov ecx,[esp+0x18]=b_lo
        //   mov eax,[esp+0x14]=a_hi → div ecx（edx:eax = 0:a_hi / b_lo）
        //   mov ebx,eax（高商）→ mov eax,[esp+0x10]=a_lo → div ecx
        //   （余:a_lo / b_lo → 低商）→ mov edx,ebx（edx=高商）
        const std::uint32_t hi_q = aa_hi / bb_lo;
        const std::uint32_t rem = aa_hi % bb_lo;
        const std::uint32_t lo_q = static_cast<std::uint32_t>(
            ((static_cast<std::uint64_t>(rem) << 32) | aa_lo) / bb_lo);
        q = (static_cast<std::uint64_t>(hi_q) << 32) | lo_q;
    } else {
        // 硬路径（0x7C9329，b_hi!=0）——与 __aulldiv 完全同构：
        // 归一化循环（shr/rcr 联合右移 a/b 直到 b_hi==0）
        std::uint32_t bh = bb_hi;
        std::uint32_t bl = bb_lo;
        std::uint32_t ah = aa_hi;
        std::uint32_t al = aa_lo;
        do {
            const std::uint32_t b_carry = bh & 1;
            bh >>= 1;
            bl = (bl >> 1) | (b_carry << 31);
            const std::uint32_t a_carry = ah & 1;
            ah >>= 1;
            al = (al >> 1) | (a_carry << 31);
        } while (bh != 0);  // or ebx,ebx → jne 循环

        // div ecx：edx:eax = a 归一化 / bl → eax=预商 esi
        const std::uint32_t esi = static_cast<std::uint32_t>(
            ((static_cast<std::uint64_t>(ah) << 32) | al) / bl);

        // 乘法校正：预商 * 原 b（b_hi/b_lo 绝对值）与 原 a 比较
        //   mul [esp+0x1c]（=b_hi）→ H；mov ecx,eax（H 低 32，高丢弃）
        //   mul [esp+0x18]（=b_lo）→ L；add edx,ecx（联合 CF）
        //   ★比较用原 a（[esp+0x14]=a_hi / [esp+0x10]=a_lo）——栈上
        //     的绝对值未移位，只有寄存器版本被归一化
        const std::uint64_t H = static_cast<std::uint64_t>(esi) * bb_hi;
        const std::uint64_t L = static_cast<std::uint64_t>(esi) * bb_lo;
        const std::uint32_t h_lo = static_cast<std::uint32_t>(H);
        const std::uint32_t l_hi = static_cast<std::uint32_t>(L >> 32);
        const std::uint32_t l_lo = static_cast<std::uint32_t>(L);
        const std::uint32_t mid = l_hi + h_lo;
        const bool cf = mid < l_hi;
        const std::uint64_t prod64 =
            (static_cast<std::uint64_t>(mid) << 32) | l_lo;
        const std::uint64_t a_abs =
            (static_cast<std::uint64_t>(aa_hi) << 32) | aa_lo;
        const std::uint32_t q32 =
            (cf || prod64 > a_abs) ? esi - 1 : esi;
        // 0x7C9366: xor edx,edx → mov eax,esi（高 32 恒 0——硬路径商
        // < 2^32，与 __aulldiv 同款怪癖）
        q = q32;
    }

    // ★符号还原（0x7C936A）：
    //   dec edi → jne 0x7C9374 —— edi 原值==1（一个负号）才取负
    //   （dec 后 0 → ZF → 不跳 → neg；edi==0/2 → dec 后非 0 → 跳）
    //   neg edx / neg eax / sbb edx,0 = 64 位取负 q = -q
    if (edi == 1) {
        q = (~q) + 1;
    }

    return static_cast<std::int64_t>(q);
}

}  // namespace ra2::crt