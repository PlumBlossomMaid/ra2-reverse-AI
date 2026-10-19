#include "crt_strcmp.h"

#include <cstdint>
#include <cstring>

namespace ra2::runtime {

int strcmp_legacy(const char* s1, const char* s2) {
    const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(s1);
    const std::uint8_t* q = reinterpret_cast<const std::uint8_t*>(s2);
    std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);

    if ((addr & 3) != 0) {
        // ★慢路径（0x7CDADC）：只对齐 s1；s2 全单字节访问
        if ((addr & 1) != 0) {
            // 1 字节路径（0x7CDAE4）：mov al,[edx]/inc edx/cmp al,[ecx]/
            // inc ecx/or al,al → je 返 0
            const std::uint8_t al = *p++;
            ++addr;
            if (al != *q) return al < *q ? -1 : 1;  // 三态（sbb/shl/inc）
            ++q;
            if (al == 0) return 0;
            // test edx,2 → je 0x7CDAA0（★已 4 对齐 → 快循环）
            if ((addr & 2) == 0) goto fast4;
            // addr%4==2 → fallthrough 2 字节路径
        }
        // 2 字节路径（0x7CDAF8）：mov ax,[edx]/add edx,2/
        // cmp al,[ecx]/or al,al → je 返 0/cmp ah,[ecx+1]/or ah,ah → je 返 0
        {
            const std::uint8_t al = p[0];
            const std::uint8_t ah = p[1];
            p += 2;
            addr += 2;
            if (al != *q) return al < *q ? -1 : 1;
            if (al == 0) return 0;
            if (ah != q[1]) return ah < q[1] ? -1 : 1;
            if (ah == 0) return 0;
            q += 2;
        }
        // add ecx,2 → jmp 0x7CDAA0（进快循环）
    }

fast4:
    // ★快路径（0x7CDAA0，s1 已 4 对齐）：4 字节小端窗口
    for (;;) {
        std::uint32_t w;
        std::memcpy(&w, p, 4);  // mov eax,[edx]（NUL 后越界读 1-3 字节，
                                // 与原版一致；NUL 检查短路不参与比较）
        const std::uint8_t b0 = static_cast<std::uint8_t>(w);
        const std::uint8_t b1 = static_cast<std::uint8_t>(w >> 8);
        const std::uint8_t b2 = static_cast<std::uint8_t>(w >> 16);
        const std::uint8_t b3 = static_cast<std::uint8_t>(w >> 24);
        if (b0 != q[0]) return b0 < q[0] ? -1 : 1;  // cmp al,[ecx]
        if (b0 == 0) return 0;                      // or al,al → je 返 0
        if (b1 != q[1]) return b1 < q[1] ? -1 : 1;  // cmp ah,[ecx+1]
        if (b1 == 0) return 0;                      // or ah,ah → je 返 0
        if (b2 != q[2]) return b2 < q[2] ? -1 : 1;  // shr 后 al=b2
        if (b2 == 0) return 0;
        if (b3 != q[3]) return b3 < q[3] ? -1 : 1;  // ah=b3
        p += 4;
        q += 4;  // add ecx,4/add edx,4
        if (b3 == 0) return 0;  // or ah,ah（b3）→ je 落到 0x7CDAD0 返 0
        // jne 循环继续
    }
}

}  // namespace ra2::runtime