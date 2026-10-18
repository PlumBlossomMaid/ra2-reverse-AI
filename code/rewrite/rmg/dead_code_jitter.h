// RA2/YR 死代码复现 —— 0x5A2ED0 族「两锚点距离 + 随机抖动」（117B × 3 副本）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：
//   0x5A2ED0 / 0x5A4AE0 / 0x5A4FA0 —— 同一源码的独立编译副本，
//   归一化后逐字节相同，唯一差异是各自私有的 5.0 常量副本地址
//   （0x7ED7C0 / 0x7ED800 / 0x7ED808）。
//
// 语义（cdecl 2 栈参 ret 8，x87 返回 double）：
//   dx = (int32)p1.x - (int32)p2.x        （movsx word 带符号读 int16）
//   dy = (int32)p1.y - (int32)p2.y
//   d2 = dx*dx + dy*dy                    （int32 imul 带符号回绕！
//                                           大坐标会溢出成负值）
//   s  = FastSqrt((double)(int32)d2)      （fild dword 带符号加载；
//                                           FastSqrt @0x4CAC40 对负数取绝对值）
//   j  = (double)(uint64)Random() * 5.0 * kInvUintMax
//                                      （fild qword 无符号读 uint32；
//                                         0x7ED898 = 1/(2^32-1)）
//   返回 s + j
//
// 取证：memory/data/rmg/deadcode_jitter_golden.txt（emu_oracle 指令级模拟，
// 5 种子 × 5 坐标对，三副本逐位一致）。
#pragma once

#include <cstdint>

#include "randomizer.h"

namespace ra2::rmg {

// 0x5A2ED0 / 0x5A4AE0 / 0x5A4FA0：两锚点距离（FastSqrt）+ 随机抖动。
// 入参为两个 int16 坐标对（原版 movsx word 读入），返回 double。
// 三副本语义完全相同（仅私有 5.0 常量地址不同），故只提供一份模型。
double DeadCodeDistanceJitter(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                              std::int16_t bx, std::int16_t by);

}  // namespace ra2::rmg
