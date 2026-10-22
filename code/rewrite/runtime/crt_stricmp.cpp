#include "crt_stricmp.h"

#include <cstdint>

namespace ra2::crt {

std::uint8_t stricmp_ascii_fold(std::uint8_t c) {
    // 原版：sub al,0x41 / cmp al,0x1A / sbb cl,cl / and cl,0x20 /
    //       add al,cl / add al,0x41
    // sbb cl,cl 在 CF=1（al-0x41 < 0x1A 无符号）时给 cl=0xFF，
    // and 0x20 → 0x20；CF=0 时 cl=0。
    std::uint8_t t = static_cast<std::uint8_t>(c - 0x41);
    if (t < 0x1A) {
        t = static_cast<std::uint8_t>(t + 0x20);
    }
    return static_cast<std::uint8_t>(t + 0x41);
}

int stricmp_legacy(CrtStricmpWorld& world, const char* s1, const char* s2) {
    if (!world.LocaleActive()) {
        // ── 快路径（[0xB782A0]==0）：纯 ASCII 折叠逐字节比较 ──
        std::uint8_t al = 0xFF;  // 哨兵：首轮 or al,al 非零保证进入循环
        for (;;) {
            if (al == 0) return 0;  // 上一轮 s2 字符为 NUL 且与 s1 全等
            al = static_cast<std::uint8_t>(*s2++);
            std::uint8_t ah = static_cast<std::uint8_t>(*s1++);
            if (ah == al) continue;  // 原始相等 → 继续
            // 折叠 s2 字符（al），然后 xchg 后折叠 s1 字符（原 al）
            std::uint8_t fa = stricmp_ascii_fold(al);   // 折叠后 s2
            std::uint8_t fb = stricmp_ascii_fold(ah);   // 折叠后 s1
            if (fb == fa) {
                al = fb;  // 折叠后相等 → 循环顶（al=折叠 s1 字符）
                continue;
            }
            // cmp al,ah（al=折叠 s1、ah=折叠 s2）→ sbb 双链
            // 折叠 s1 < 折叠 s2（无符号）→ -1，否则 +1
            return (fb < fa) ? -1 : 1;
        }
    }

    // ── 慢路径（[0xB782A0]!=0）：锁 + 每字符 tolower ──
    world.AcquireLock();
    std::uint8_t al = 0xFF;  // 哨兵（mov eax,0xFF：高位置 0，无污染）
    int result = 0;
    for (;;) {
        if (al == 0) {
            result = 0;  // 两串全等走到 NUL
            break;
        }
        al = static_cast<std::uint8_t>(*s2++);
        std::uint8_t bl = static_cast<std::uint8_t>(*s1++);
        if (al == bl) continue;  // 原始相等 → 继续
        // 先 tolower(s1) 后 tolower(s2)（原版 push eax 存 s2、第一参
        // push ebx=s1，第二次调用复用栈上 s2）
        std::uint32_t fb = world.LocaleToLower(bl);
        std::uint32_t fa = world.LocaleToLower(al);
        if (fb == fa) {
            al = static_cast<std::uint8_t>(fb);
            continue;
        }
        result = (fb < fa) ? -1 : 1;
        break;
    }
    world.ReleaseLock();
    return result;
}

}  // namespace ra2::crt