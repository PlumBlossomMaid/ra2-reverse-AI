#include "crt_strnicmp.h"

#include <cstdint>

namespace ra2::crt {

int strnicmp_legacy(const char* s1, const char* s2, std::size_t count) {
    if (count == 0) return 0;  // or ecx,ecx → je 0x7CD77A
    // 快路径：C locale（[0xB782A0]==0）。慢路径（locale 非 C）依赖锁家族
    // 0x7CD9F5 + tolower 内核 0x7CB063（留桩边界，见头注释）。
    std::size_t i = 0;
    for (;;) {
        // ah=[esi] → or ah,ah → mov al,[edi] → je 收尾（ah==0）
        // → or al,al → je 收尾（al==0）——NUL 早退时 ah/al 未转小写
        // （转小写在 inc 之后），但 NUL=0 与任何非 0 字节比较结果不受
        // 是否转小写影响：0x00 恒定小于一切，'A'-'Z' 恒大于 0x00。
        const std::uint8_t ah_raw =
            static_cast<std::uint8_t>(s1[i]);
        const std::uint8_t al_raw =
            static_cast<std::uint8_t>(s2[i]);
        if (ah_raw == 0 || al_raw == 0) {
            if (ah_raw == al_raw) return 0;   // je → 0
            return ah_raw < al_raw ? -1 : 1;  // jb → -1 / neg → +1
        }
        ++i;  // inc esi / inc edi
        // 转小写：cmp ah,bh / jb → cmp ah,bl / ja → add ah,dh
        // （仅 [0x41,0x5A] 内 +0x20；0x5A='Z'，0x7A='z' 不转）
        const std::uint8_t ah = (ah_raw >= 0x41 && ah_raw <= 0x5A)
                                    ? static_cast<std::uint8_t>(ah_raw + 0x20)
                                    : ah_raw;
        const std::uint8_t al = (al_raw >= 0x41 && al_raw <= 0x5A)
                                    ? static_cast<std::uint8_t>(al_raw + 0x20)
                                    : al_raw;
        if (ah != al) {
            // 0x7CD6DF：cmp ah,al → jb 返回 -1 / neg → +1
            return ah < al ? -1 : 1;
        }
        if (--count == 0) {
            // dec ecx → jne 循环：count 耗尽落到收尾，ah==al（循环条件
            // 已检查相等），收尾 cmp ah,al → je → 0
            return 0;
        }
    }
}

}  // namespace ra2::crt