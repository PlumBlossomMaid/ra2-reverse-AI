#include "crt_aulldiv.h"

#include <cstdint>

namespace ra2::runtime {

std::uint64_t aulldiv_legacy(std::uint64_t a, std::uint64_t b) {
    const std::uint32_t a_lo = static_cast<std::uint32_t>(a);
    const std::uint32_t a_hi = static_cast<std::uint32_t>(a >> 32);
    const std::uint32_t b_lo = static_cast<std::uint32_t>(b);
    const std::uint32_t b_hi = static_cast<std::uint32_t>(b >> 32);

    if (b_hi == 0) {
        // 快速路径（0x7C938A，b_hi==0）：
        //   mov ecx,[esp+0x14]=b_lo
        //   mov eax,[esp+0x10]=a_hi → div ecx（edx:eax = 0:a_hi / b_lo）
        //   mov ebx,eax（ebx=高商）→ mov eax,[esp+0xC]=a_lo → div ecx
        //   （edx:eax = 余:a_lo / b_lo → eax=低商）→ mov edx,ebx
        // div 指令 64/32：商在 eax、余在 edx；余数接力第二次 div
        const std::uint32_t hi_q = a_hi / b_lo;   // eax（高商）
        const std::uint32_t rem = a_hi % b_lo;    // edx（余）
        // div2：edx:eax = (rem<<32 | a_lo) / b_lo → eax=低商
        // ★余<b_lo ⇒ 被除数 < b_lo*2^32 ⇒ 商 < 2^32 永不溢出（原版契约）
        const std::uint32_t lo_q = static_cast<std::uint32_t>(
            ((static_cast<std::uint64_t>(rem) << 32) | a_lo) / b_lo);
        return (static_cast<std::uint64_t>(hi_q) << 32) | lo_q;
    }

    // 硬路径（0x7C93A2，b_hi!=0）
    // 归一化循环（0x7C93B0）：a/b 同步右移直到 b_hi==0
    //   shr ecx,1 / rcr ebx,1（b_hi:b_lo 联合右移，CF 传位）
    //   shr edx,1 / rcr eax,1（a_hi:a_lo 联合右移）
    std::uint32_t bh = b_hi;
    std::uint32_t bl = b_lo;
    std::uint32_t ah = a_hi;
    std::uint32_t al = a_lo;
    do {
        const std::uint32_t b_carry = bh & 1;
        bh >>= 1;
        bl = (bl >> 1) | (b_carry << 31);
        const std::uint32_t a_carry = ah & 1;
        ah >>= 1;
        al = (al >> 1) | (a_carry << 31);
    } while (bh != 0);  // or ecx,ecx → jne 循环

    // div ebx（0x7C93BC）：edx:eax = a 归一化 / bl → eax=预商 esi
    // ★归一化后 bl 最高位=原 b_hi 最高位 ⇒ bl ≥ 2^31 ⇒ 预商不溢出
    const std::uint32_t esi = static_cast<std::uint32_t>(
        ((static_cast<std::uint64_t>(ah) << 32) | al) / bl);

    // 乘法校正（0x7C93C0-0x7C93DC）：
    //   mul [esp+0x18]（=b_hi）→ edx:eax = esi*b_hi
    //   mov ecx,eax（★H 低 32；★★H 高 32 被丢弃——原版不检查！）
    //   mov eax,[esp+0x14]（=b_lo）→ mul esi → edx:eax = b_lo*esi（L）
    //   add edx,ecx（★积低64 高32 = L_hi + H_lo，CF=进位）
    //   jb 0x7C93DE（CF=1 → 减商）
    //   cmp edx,a_hi → ja 减商 / jb 收尾 → cmp eax,a_lo → jbe 收尾
    //   （★积低64 > a 时减商）
    const std::uint64_t H = static_cast<std::uint64_t>(esi) * b_hi;
    const std::uint64_t L = static_cast<std::uint64_t>(esi) * b_lo;
    const std::uint32_t h_lo = static_cast<std::uint32_t>(H);  // ecx
    const std::uint32_t l_hi = static_cast<std::uint32_t>(L >> 32);
    const std::uint32_t l_lo = static_cast<std::uint32_t>(L);
    const std::uint32_t mid = l_hi + h_lo;      // add edx,ecx（32 位回绕）
    const bool cf = mid < l_hi;                 // ★CF=进位
    const std::uint64_t prod64 =
        (static_cast<std::uint64_t>(mid) << 32) | l_lo;
    // 校正：CF（L_hi+H_lo 溢出）或 积低64 > a → dec esi
    const std::uint32_t q = (cf || prod64 > a) ? esi - 1 : esi;

    // 0x7C93DF: xor edx,edx → mov eax,esi（高 32 恒 0——商 < 2^32？
    // ★注意：__aulldiv 返回 64 位商，但 esi 是预商的低 32……原版此处
    // 高 32 置 0。实际 64 位商可能 ≥ 2^32 吗？——原版归一化后 div
    // 的商在 eax（32 位），高 32 硬置 0（edx=0）：对 64/64 除法若商
    // ≥ 2^32 原版即截断——但归一化保证预商 < 2^32，故数学上商的高
    // 32 应为 0。（大商场景：b_hi!=0 且 a 远大于 b 时归一化后仍成立）
    return q;
}

}  // namespace ra2::runtime