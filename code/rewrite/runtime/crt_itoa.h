// _itoa 家族逐字节复刻模型 —— batch 1043
//
// 0x7D46B9(92B) = MSVC6 CRT _ltoa/_ultoa 共享核心（_itoa core），
// cdecl 4 栈参 (value, buffer, radix, sign)，返回 eax（反转残留）：
//   sign != 0 → 写 '-' 且 value **无符号取负**（neg，INT_MIN 自反）；
//   主循环 div **两遍**（第一遍余数存 ebx、第二遍商进 esi —— 冗余
//   照抄）；余数 >9 → +0x57（小写 hex），<=9 → +0x30；NUL 终止后
//   edi/ecx 双指针原地反转（do-while）。
//   ★eax 残留 = 反转循环最后 `mov al,[ecx]` 的低字节（高 24 位 = 最后
//   一次 div 商 = 0）——调用方（_ultoa/_ltoa 等）自己恢复 buffer 指针，
//   核心返回值约定不依赖。
//
// 0x7D473F(27B) = `_ultoa` 薄包装：值按无符号位型格式化（恒 sign=0，
// 只补第 4 参 0），返回 buffer。golden 实锤：value=-12345 radix=10 →
// "4294954951"（无符号放大）。
//
// 0x7D4715(42B) = `_ltoa` 薄包装：sign = (radix==10 && value<0) ? 1 : 0
// （jne/jge 判定），返回 buffer。golden 实锤：value=-12345 radix=16 →
// "ffffcfc7"（radix!=10 → 无负号 + 无符号 hex）。
//
// ★怪癖（golden 实证，照抄）：
//   ① sign=1 时 neg 无条件取负——正数 value 也会被翻转
//     （2147483647, sign=1 → "-2147483649"，golden 实证）；
//   ② div 两遍冗余（第一遍余数、第二遍商——eax 被重置后重除）；
//   ③ 余数 >9 用小写 hex（+0x57 而非 +0x37）——`_ltow` 式小写输出；
//   ④ 数字先低后高写入 + 原地反转（NUL 在反转前已落下，指针不相交）；
//   ⑤ INT_MIN 取负自反（neg 回绕）——照抄不额外处理；
//   ⑥ 核心不设 buffer 返回（eax=反转残留），包装层自己恢复。

#ifndef RA2_RUNTIME_CRT_ITOA_H_
#define RA2_RUNTIME_CRT_ITOA_H_

#include <cstdint>

namespace ra2::runtime {

// 0x7D46B9《_itoa 核心》：无符号位型 value 按 radix 格式化到 buffer，
// sign != 0 时写 '-' 并取负。返回 eax 残留（低 8 位 = 反转循环最后
// al，高 24 位 = 0）。radix 走无符号 div（负数 radix 未定义，原版 div）。
std::uint32_t itoa_core_legacy(std::int32_t value, char* buffer,
                               std::int32_t radix, std::int32_t sign);

// 0x7D473F `_ultoa`：value 按无符号位型格式化，返回 buffer。
char* ultoa_legacy(std::uint32_t value, char* buffer, std::int32_t radix);

// 0x7D4715 `_ltoa`：十进制负数带负号（sign 判定），其余同核心，返回 buffer。
char* ltoa_legacy(std::int32_t value, char* buffer, std::int32_t radix);

}  // namespace ra2::runtime

#endif  // RA2_RUNTIME_CRT_ITOA_H_