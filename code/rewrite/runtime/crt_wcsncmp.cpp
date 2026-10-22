#include "crt_wcsncmp.h"

#include <cstdint>

namespace ra2::crt {

int wcsncmp_legacy(const std::uint16_t* s1, const std::uint16_t* s2,
                   std::size_t count) {
    if (count == 0) return 0;  // cmp [ebp+0x10],0 → jne → xor eax,eax
    --count;                    // dec dword ptr [ebp+0x10]
    if (count == 0) goto done;  // je 0x7CB4FA（dec 后为 0 → 直奔收尾）
    for (;;) {
        const std::uint16_t a = *s1;
        if (a == 0) goto done;    // test ax,ax → je 收尾（s1 遇 NUL）
        if (a != *s2) goto done;  // cmp ax,[edx] → jne 收尾
        ++s1;
        ++s2;                       // inc ecx ×2 / inc edx ×2（2B 步进）
        if (--count != 0) continue;  // dec → jne 循环（Count 限制）
        break;
    }
done:
    // 0x7CB4FA: movzx eax,word[ecx] / movzx ecx,word[edx] / sub eax,ecx
    // ★返回无符号字符差（非三态）——s1 遇 NUL 时为 0 - s2[当前]（负值）
    return static_cast<int>(*s1) - static_cast<int>(*s2);
}

}  // namespace ra2::crt