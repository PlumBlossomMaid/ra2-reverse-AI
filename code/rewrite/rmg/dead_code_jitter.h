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

// 0x59D480：float 版两锚点距离 + 随机抖动 + arg3 权重（cdecl 3 栈参 ret 0xC）。
// 真义（黄金实证，推翻 docs/dead-code-functions.md:149「+0.5*arg−0.02*euclid」——
// 逐字节追栈证明 [esp+0xC]（pop esi 后）是 sqrt 结果 float32 槽、[esp+0x14] 才是
// 第三栈参 arg3）：
//   dx/dy = p1-p2 坐标差（movsx word）；d2 = dx*dx+dy*dy（int32 imul 回绕）
//   s32 = ChopFloat(FastSqrt((double)(int32)d2))    // fstp dword 窄化（float32）
//   u   = (double)(uint64)Random()                  // fild qword 无符号扩展
//   返回 u * kInvUintMax * 10.0f + 0.5f * (double)s32
//        - 0.02f * (double)(int32)arg3              // 全程 RC=chop
// 权重常量：0x7ED768=10.0f / 0x7ED764=0.5f / 0x7ED76C=0.02f（.rdata 实证）。
// 取证：memory/data/rmg/deadcode_jitter59d480_golden.txt（100 例，含 arg3 维度）。
double DeadCodeDistanceJitterF(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                               std::int16_t bx, std::int16_t by, std::int32_t arg3);

// 0x5A5AE0：切比雪夫距离 + 随机抖动（cdecl 2 栈参 ret 8——两个 int16 坐标对
// 指针，x87 返回 double，107B）。同族模板（base + Random()*k*inv）但距离度量
// 为切比雪夫 max(|dx|,|dy|)（非欧氏 sqrt），抖动系数 3.0（[0x7ED818]，0x5A2ED0
// 族是 5.0）。
//   dx = p1.x-p2.x ; dy = p1.y-p2.y（movsx word 有符号读 int16）
//   adx = |dx| ; ady = |dy|（cdq/xor/sub 无分支绝对值——坐标差不溢出 int32）
//   base = max(adx, ady)
//   u   = (double)(uint64)Random()（fild qword 无符号扩展）
//   j   = u * 3.0 * kInvUintMax（[0x7ED898]=kInvUintMax；RC=chop 链）
//   返回 base + j（fild dword 有符号 base + faddp）
// 取证：memory/data/rmg/deadcode_5ae0_golden.txt（emu_oracle 指令级模拟，
// 5 种子 × 8 坐标对，40 行全对拍）。
double DeadCodeChebyshevJitter(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                               std::int16_t bx, std::int16_t by);

// 0x5A1190：atan2 象限归一化 + 角度抖动（cdecl 3 栈参 ret 0x10——p1/p2 坐标
// 指针 + arg double 8B，x87 返回 double）。TSV:1028 已 semantics 登记。
// 真义（黄金实证 + 机器码逐段确认）：
//   dx = p2.x-p1.x ; dy = p2.y-p1.y（movsx word 带符号读 int16）
//   若 dx==0: a = π/2（[0x7E2820] 快速路径）
//   否则:     a = Atan(-(double)dy/(double)dx)  // 0x4CADE0 查表 atan（fast_math 模型）
//             if (dx < 0) a += π                // [0x7E44D0]
//   r = |a - arg|（fsub qword + fabs）
//   while (r >= 2π) r -= 2π                     // mod 2π（[0x7E3CC0]）
//   if (r > π) r = 2π - r                       // 折叠到 [0,π]
//   返回 Random() * kInvUintMax * 2.0 + 1.5 * r // [0x7ED898]×[0x7ED790]=2.0+[0x7ED798]=1.5
// 取证：memory/data/rmg/deadcode_angle_golden.txt（emu 指令级模拟）。
double DeadCodeAngleJitter(Randomizer& rng, std::int16_t ax, std::int16_t ay,
                           std::int16_t bx, std::int16_t by, double arg);

}  // namespace ra2::rmg
