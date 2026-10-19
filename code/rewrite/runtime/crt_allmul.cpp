#include "crt_allmul.h"

#include <cstdint>

namespace ra2::runtime {

uint64_t allmul_legacy(std::uint64_t a, std::uint64_t b) {
    const std::uint32_t a_lo = static_cast<std::uint32_t>(a);
    const std::uint32_t a_hi = static_cast<std::uint32_t>(a >> 32);
    const std::uint32_t b_lo = static_cast<std::uint32_t>(b);
    const std::uint32_t b_hi = static_cast<std::uint32_t>(b >> 32);

    if ((a_hi | b_hi) == 0) {
        // 快速路径：or ecx,eax → jne 不跳（a_hi|b_hi==0）→
        // mov eax,[esp+4]=a_lo → mul ecx → edx:eax = a_lo*b_lo
        return static_cast<std::uint64_t>(a_lo) * b_lo;
    }

    // 硬路径（模拟原版寄存器序列，逐条对应）：
    //   push ebx
    //   mul ecx               ; edx:eax = a_hi*b_lo（eax=a_hi, ecx=b_lo）
    //   mov ebx,eax           ; ebx = 低32
    //   mov eax,[esp+8]=a_lo  ; ★push 后 esp-4，原 [esp+0xC] 变 [esp+8]
    //   mul [esp+0x14]=b_hi   ; edx:eax = a_lo*b_hi
    //   add ebx,eax           ; ebx += 低32（★32 位回绕）
    //   mov eax,[esp+8]=a_lo  ; 再取 a_lo
    //   mul ecx               ; edx:eax = a_lo*b_lo
    //   add edx,ebx           ; ★edx += ebx（32 位回绕，溢出丢弃）
    //   pop ebx
    // ★a_hi*b_hi 项完全丢弃（mod 2^64）；两次 add 均 32 位回绕
    std::uint32_t ebx = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(a_hi) * b_lo);  // mov ebx,eax（低32）
    ebx += static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(a_lo) * b_hi);  // add ebx,eax（回绕）

    const std::uint64_t t = static_cast<std::uint64_t>(a_lo) * b_lo;
    const std::uint32_t eax = static_cast<std::uint32_t>(t);
    const std::uint32_t edx =
        static_cast<std::uint32_t>(t >> 32) + ebx;  // add edx,ebx（回绕）
    return (static_cast<std::uint64_t>(edx) << 32) | eax;
}

}  // namespace ra2::runtime