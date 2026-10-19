#include "crt_strchr.h"

#include <cstdint>

namespace ra2::runtime {

const char* strchr_legacy(const char* s, int ch) {
    // al = ch 零扩展低 8 位
    const std::uint8_t target = static_cast<std::uint8_t>(ch);
    // 对齐探测（0x7CAF40）：未对齐时逐字节
    std::uintptr_t edx = reinterpret_cast<std::uintptr_t>(s);
    while ((edx & 3u) != 0) {
        const std::uint8_t c = *reinterpret_cast<const std::uint8_t*>(edx);
        ++edx;
        if (c == target) return reinterpret_cast<const char*>(edx - 1);
        if (c == 0) return nullptr;  // test cl,cl / je 0x7CAFA4
    }
    // 四字节 pattern（0x7CAF5B）：xor eax,eax 已并入 target 展开
    std::uint32_t pattern = target;
    pattern |= (static_cast<std::uint32_t>(target) << 8);
    pattern |= (static_cast<std::uint32_t>(target) << 16);
    pattern |= (static_cast<std::uint32_t>(target) << 24);
    // 主循环（0x7CAF66 四字节批量扫描，0x7EFEFEFF/0x81010100 magic）
    for (;;) {
        const std::uint32_t v = *reinterpret_cast<const std::uint32_t*>(edx);
        const std::uint32_t esi = v + 0x7EFEFEFFu;      // NUL 检测 A
        const std::uint32_t x = v ^ pattern;            // 命中检测
        const std::uint32_t edi = x + 0x7EFEFEFFu;      // 命中检测 A
        const std::uint32_t hit = (x ^ 0xFFFFFFFFu) ^ edi;
        const std::uint32_t nul = (v ^ 0xFFFFFFFFu) ^ esi;
        edx += 4;
        if ((hit & 0x81010100u) != 0) {                 // jne 0x7CAFA8
            // 逐字节回验（取回 edx-4 的 dword）
            const std::uint32_t back =
                *reinterpret_cast<const std::uint32_t*>(edx - 4);
            const std::uint8_t b0 = static_cast<std::uint8_t>(back);
            const std::uint8_t b1 = static_cast<std::uint8_t>(back >> 8);
            const std::uint8_t b2 = static_cast<std::uint8_t>(back >> 16);
            const std::uint8_t b3 = static_cast<std::uint8_t>(back >> 24);
            if (b0 == target) return reinterpret_cast<const char*>(edx - 4);
            if (b0 == 0) return nullptr;
            if (b1 == target) return reinterpret_cast<const char*>(edx - 3);
            if (b1 == 0) return nullptr;
            if (b2 == target) return reinterpret_cast<const char*>(edx - 2);
            if (b2 == 0) return nullptr;
            if (b3 == target) return reinterpret_cast<const char*>(edx - 1);
            if (b3 == 0) return nullptr;
            continue;  // jmp 0x7CAF66
        }
        if ((nul & 0x81010100u) != 0) {                 // jne 0x7CAF93
            if ((nul & 0x1010100u) != 0) return nullptr;  // jne 0x7CAFA2
            if ((esi & 0x80000000u) == 0) return nullptr; // jne 0x7CAF66
            continue;  // and esi,0x80000000 / jne 0x7CAF66
        }
    }
}

}  // namespace ra2::runtime