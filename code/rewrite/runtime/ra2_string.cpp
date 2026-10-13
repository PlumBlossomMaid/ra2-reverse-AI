#include "ra2_string.h"

#include <cstdint>

namespace ra2::runtime {

// 共享空串常量在 0x889F64（.data 区）。原版所有窄字符串类的空指针回退
// 都指到这里（0x7B5400/0x7B5440 c_str()、0x7B46xx 族）。内容应为单个 '\0'。
const char* const kEmptyString = "";

const char* narrow_cstr_legacy(const void* self) {
    // 0x7B5400 / 0x7B5440（逐字节相同的孪生桩）：this->[0] 为 char*，
    // NULL 时回退共享空串常量。
    const char* p = *static_cast<const char* const*>(self);
    return p ? p : kEmptyString;
}

char narrow_char_at_legacy(const void* self, std::uint32_t index) {
    // 0x7B5410 / 0x7B5450 / 0x7B5480（35B 孪生族三胞胎）：repne scasb 求
    // strlen（or ecx,-1 / xor eax,eax / repne scasb / not ecx / dec ecx），
    // 下标 >= 长度返回 0，否则返回 str[index]。
    const char* p = *static_cast<const char* const*>(self);
    std::uint32_t len = 0;
    if (p) {
        while (p[len] != '\0') {
            ++len;
        }
    }
    return index >= len ? '\0' : p[index];
}

}  // namespace ra2::runtime