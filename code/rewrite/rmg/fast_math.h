// RA2/YR 随机地图生成器 —— 原版数学基元
//
// 这三个基元是地图"形状"的直接来源，且都不能用标准库替代：
//
//   FastSqrt      @ 0x4CAC40  16384 项查表近似开方，只有约 14 位精度
//   UniformUnit   @ 0x598000  (double)(uint32)Random() * (1/4294967295.0)
//   NormalDeviate @ 0x5980C0  Marsaglia 极坐标法正态分布，内部用 x87 fyl2x 求 ln
//
// 涂陆地的堆扩散（0x59BBC0）每推进一格，就用两个正态偏差让"引力中心"随机游走，
// 岛屿的不规则形状正来自于此；而每个候选格的优先级 = FastSqrt(到中心距离平方)。
// 用 std::sqrt 替换 FastSqrt 会在千分之几的量级上改变优先级排序，进而改变地形。
//
// 全部浮点运算必须在原版环境下进行（FpuChopScope：53 位双精度 + 向零截断），
// 见 scaled_random.h 的说明。
#pragma once

#include <cstdint>

#include "randomizer.h"

namespace ra2::rmg {

// 0x4CAC40：查表近似开方。
// 取被开方数的浮点位形：尾数高 14 位查表得结果尾数，指数减半得结果指数。
// 表写死在 sqrt_table.h —— 它是数学常量表（table[i] = isqrt(N_i) - 2^23），
// 回归测试用纯整数运算重算全表自检，保证"结果与架构无关"在编译期即成立。
// 原版返回 float（再由调用方加宽成 double），这里保持同一语义。
double FastSqrt(double x);

// 原版用 x87 `fyl2x` 求自然对数（`fldln2; fld s; fyl2x`），全局 FPU 为 PC=53/RC=chop。
//
// 语义定为「**精确 ln 向零截断到 double**」，由 dd_log.h 的双双精度实现给出，
// **所有架构（含 x86）走同一条路**。理由见 docs/random-map/fast-math.md §四：
// 真 CPU 实测 20000 例中 `fyl2x` 有 7 例（0.035%）比该定义低 1 ulp——
// 是 `fyl2x` 自身内部近似不足以正确舍入，而非本定义有误。
// 既然 `fyl2x` 本就不是正确舍入的，钉死"本机 fyl2x 给什么就是什么"会让 x86 的
// 输出依赖具体 CPU 型号；改用唯一确定的数学定义，才能让各架构互相逐位一致。
// 这 1 ulp 不会传到下游：唯一消费者是 FastSqrt，它只取 float 尾数高 14 位
// （量化步长 2^-14），2^-52 的扰动翻转它的概率约 2^-38，实测 20000 例 0 次。
double ChopLog(double s);

#if defined(_M_IX86) || defined(__i386__)
// 仅供测试取证：直接内联那条 `fyl2x`。产品代码不调用它。
double ChopLogX87(double s);
#endif

// 0x598000：均匀分布 [0, 1]，与 scaled_random 用的是同一个比例常数
double UniformUnit(Randomizer& rng);

// 0x5980C0（对象 @0xABDFB8，由 0x598960 与全局 Randomizer 一并初始化）
//   对象布局：+0x00 char 缓存标志 / +0x08 double 缓存值 / +0x10 均匀源函数指针
// Marsaglia 极坐标法一次算出两个正态偏差，第二个缓存起来下次直接返回——
// 这意味着**调用次数的奇偶性会改变随机数流的消耗节奏**，复刻时不可省略缓存。
class NormalDeviate {
 public:
  explicit NormalDeviate(Randomizer& rng) : rng_(&rng) {}

  double operator()();

  // 原版 0x598960 初始化时把缓存标志清零
  void Reset() { has_cached_ = false; }

  bool has_cached() const { return has_cached_; }

 private:
  Randomizer* rng_;
  bool has_cached_ = false;
  double cached_ = 0.0;
};

}  // namespace ra2::rmg
