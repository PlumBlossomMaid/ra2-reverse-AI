#pragma once

#include <cstdint>

namespace ra2::runtime {

// 窄字符串访问器家族（0x7B54xx 区）。原版的字符串类是「单指针字段」布局：
// this->[0] 是 char*，为空（NULL）时所有读取走共享空串常量。这里按原样保留
// 指针语义，不引入 std::string —— 原版的 NULL 与空串是两种状态，
// c_str() 会把 NULL 折叠成空串而字段仍是 NULL，这个差异下游可见。

// 共享空串常量 [0x889F64]：this->[0] 为 NULL 时 c_str() 返回它。
extern const char* const kEmptyString;

// 0x7B5400 / 0x7B5440（逐字节相同的孪生桩，44 与 48 callers）：
// 返回内部 char*，NULL 则返回共享空串常量。
const char* narrow_cstr_legacy(const void* self);

// 0x7B5410 / 0x7B5450 / 0x7B5480（35B 孪生族三胞胎，逐字节相同）：
// 用 repne scasb 求 strlen，下标越界返回 0，否则返回 str[index]。
char narrow_char_at_legacy(const void* self, std::uint32_t index);

// 0x7B4780: 窄字符串 operator==（41 callers）。双方指针为 NULL 视为相等，
// 一方 NULL 一方非 NULL 视为不等；否则逐字节比较（2 字节展开循环形态），
// 首字节即决定返回值（sbb 归一化，不比较剩余部分）。
bool narrow_equals_legacy(const void* lhs, const void* rhs);

}  // namespace ra2::runtime