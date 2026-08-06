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
// 表项可由纯整数运算重算：table[i] = isqrt(N_i) - 2^23，无需搬运二进制资产。
// 原版返回 float（再由调用方加宽成 double），这里保持同一语义。
double FastSqrt(double x);

// x87 `fyl2x` 求自然对数。在 PC=53/RC=chop 下等价于「精确 ln 向零截断到 double」
// （已用高精度有理数重演对拍 80/80，见 docs/random-map/fast-math.md）。
double ChopLog(double s);

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
