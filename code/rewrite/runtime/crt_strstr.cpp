#include "crt_strstr.h"

#include "crt_strchr.h"

namespace ra2::crt {

const char* strstr_legacy(const char* str, const char* sub) {
    const char* s = str;
    // SubStr 空 → 返回 Str 原址（0x7CA52A）
    if (sub[0] == 0) return s;
    // 单字符路径（0x7CA517）→ jmp 0x7CAF36 借 strchr 内核
    if (sub[1] == 0) return strchr_legacy(s, sub[0]);
    // 主扫描（0x7CA4C8）：找 SubStr[0]
    for (;;) {
        // 首字符命中检查（Str 游标）
        if (*s == sub[0]) {
            // 命中比对（0x7CA4EA）：SubStr[1]
            if (*(s + 1) == sub[1]) {
                const char* hit = s;
                // 长串匹配（0x7CA4FB）：SubStr[2..]
                std::size_t k = 2;
                for (;;) {
                    if (sub[k] == 0) return hit;   // SubStr 结束 → 返命中
                    if (s[k] != sub[k]) break;     // 不匹配 → 回扫描
                    ++k;
                }
            }
        }
        if (*s == 0) return nullptr;  // Str 到 NUL 未找到 → 返 0
        ++s;
    }
}

}  // namespace ra2::crt