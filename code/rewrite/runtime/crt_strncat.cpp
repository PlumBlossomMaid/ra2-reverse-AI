#include "crt_strncat.h"

#include <cstddef>

namespace ra2::crt {

char* strncat_legacy(char* dst, const char* src, std::size_t count) {
    // 0x7CB550: mov ecx,[esp+0xC]=count → test ecx,ecx → je 0x7CB604
    // （★★count==0 早退——直接返 dst，不写任何东西——与 wcsncat 的
    //  count==0 写 NUL 不同，照抄各自行为）
    if (count == 0) {
        return dst;
    }

    // 扫 dst 尾 NUL：0x7CB55D mov edi,[esp+8]=dst → 逐字节扫（未对齐）
    // → 对齐后 4 字节窗口 0x7EFEFEFF 魔术检 NUL（0x7CB57A-0x7CB5B8）；
    // 语义等价 = 找到 dst 第一个 NUL 的位置 p（写入起点）
    char* p = dst;
    while (*p != 0) {
        ++p;
    }

    // 复制：0x7CB5BB mov esi,[esp+0x14]=src → 未对齐慢复制逐字节
    // （0x7CB5D0：dl=[esi]; inc esi; test dl,dl → je 0x7CB60A 写 NUL
    //  终止；否则 mov [edi],dl; inc edi; dec ecx → je 0x7CB600 补 NUL）
    // → 对齐后 dword 窗口复制（0x7CB614/0x7CB61C 含 0x7EFEFEFF 检
    //  NUL——命中按字节位回退写含 NUL 的部分提前终止）；
    // 语义等价 = 最多复制 count 个 src 字符，src NUL 也会被复制进
    // dst 并停止（自然终止），count 耗尽则补 NUL。
    const char* s = src;
    std::size_t n = count;
    while (n > 0) {
        const char ch = *s;
        *p = ch;  // ★src 的 NUL 会被复制进 dst（0x7CB60A/0x7CB64E 等
                  //   终止路径均写含 NUL 的字节/word/dword——照抄）
        ++p;
        ++s;
        if (ch == 0) {
            return dst;  // src NUL → 自然终止
        }
        --n;
    }
    *p = 0;  // ★count 耗尽 → 补 NUL（0x7CB600 mov [edi],cl——ecx 已 0）
    return dst;
}

}  // namespace ra2::crt