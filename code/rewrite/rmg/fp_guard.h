// 浮点环境守卫——把「跨平台逐位一致」的前提变成编译期/启动期可检查的事实
//
// 我们的对拍标准是**位形相等**而不是容差，好处是任何平台差异都会在第一个数值上
// 立刻暴露，不会攒到某个阈值才被发现；代价是必须把所有会改变位形的编译器/CPU
// 行为逐条钉死。已知的坑：
//
//  1. x87 超额精度：32 位 x86 上若用 x87 做 double 运算，中间结果是 80 位，
//     `a*b + c*d` 会与 SSE2 不同。GCC/Clang 32 位默认就是 x87。
//     -> 编译期断言 FLT_EVAL_METHOD == 0，并在 CMake 里强制 -mfpmath=sse
//  2. FMA 收缩：ARM / LoongArch / 带 FMA 的 x86 上，编译器可能把 `v*v + u*u`
//     融合成一条 fma，少一次舍入 -> 位形不同。这是**最容易中招**的一条。
//     -> #pragma FP_CONTRACT OFF + CMake 加 -ffp-contract=off
//  3. 舍入模式被忽略：-ffast-math / -Ofast 下 fesetround 可能不起作用。
//     -> #pragma FENV_ACCESS ON，并由 FpuRoundingModeWorks() 在测试里实测
//  4. 有符号右移：C++17 里负数 >> 是实现定义（C++20 才规定为算术右移）。
//     -> 断言当前实现确为算术右移
//
// 参考做法来自数值库的常见工程实践（如 Eigen / Paddle 对 FMA 与 fenv 的处理）。
#pragma once

#include <cfenv>
#include <cfloat>
#include <climits>

#if defined(_MSC_VER)
#pragma fenv_access(on)
#pragma fp_contract(off)
#elif defined(__clang__)
#pragma STDC FENV_ACCESS ON
#pragma STDC FP_CONTRACT OFF
#endif
// GCC 不支持这两个 pragma，靠 CMake 传 -ffp-contract=off / -frounding-math

namespace ra2::rmg {

// 1. 浮点中间精度：原版 gamemd.exe 用 MSVC 6.0 编译，x87 全程 80 位扩展精度
// （FLT_EVAL_METHOD == 2）。要逐位复刻原版，CLI 也必须用 x87。
// x64 / SSE2（FLT_EVAL_METHOD == 0）在 NormalDeviate 拒绝采样上会与原版分岔。
// x86 + /arch:IA32 是唯一能匹配原版精度的构建配置。
#if defined(FLT_EVAL_METHOD)
static_assert(FLT_EVAL_METHOD == 0 || FLT_EVAL_METHOD == 2 || FLT_EVAL_METHOD == -1,
              "浮点中间精度既不是 double 也不是 x87 80 位："
              "用 /arch:IA32 (x86) 或 -mfpmath=sse (GCC) 确保其中一种");
#endif

// 2. 必须是 IEEE-754 双精度
static_assert(sizeof(double) == 8, "double 必须是 8 字节 IEEE-754");
static_assert(DBL_MANT_DIG == 53, "double 必须是 53 位尾数");
static_assert(FLT_RADIX == 2, "浮点基数必须是 2");

// 3. 负数右移必须是算术右移（原版 `sar ax,1` 的语义）
static_assert((-2 >> 1) == -1, "需要算术右移");

// 运行期探针：舍入模式是否真的生效。
// -ffast-math 之类会让 fesetround 变成空操作，那样所有对拍都会以极难定位的方式失败。
// 测试里先跑这个，失败信息才有意义。
bool FpuRoundingModeWorks();

// FMA 收缩没有可靠的运行期探针（探针本身会被优化掉）：
// 它由 CMake 的 -ffp-contract=off / #pragma FP_CONTRACT OFF 禁止；
// 万一漏网，NormalDeviate 的黄金对拍会在第一个数值上直接失败——
// 这正是「比位形而不比容差」的价值：不需要猜阈值，错了就是错了。

}  // namespace ra2::rmg
