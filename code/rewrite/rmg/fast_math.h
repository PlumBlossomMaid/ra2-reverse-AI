// RA2/YR 随机地图生成器 —— 原版数学基元
//
// 这几个基元是地图"形状"的直接来源，且都不能用标准库替代：
//
//   FastSqrt      @ 0x4CAC40  16384 项查表近似开方，只有约 14 位精度
//   Sin / Cos     @ 0x4CACB0 / 0x4CAD00  10240 项查表，1/8192 圈的量化角
//   Atan          @ 0x4CADE0  4097 项查表，步长 0.0244133547，|x|>100 直接给 π/2
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

// 0x4CACB0 / 0x4CAD00：查表正弦、余弦。**不是** std::sin/std::cos。
//
// 两者共用 sin_table.h 那张 10240 项 float 表（原版 0x84F084），走法完全一样：
//   idx  = _ftol(x * 2607.594482421875f)        // 常数 = chop_f(8192/π)，向零截断
//   half = trunc(idx / 2) & 0x1FFF              // 折半 → 以 2π/8192 为步长的角
//   if (idx 是奇数 && half < 上限) ++half       // 奇数位再补半步（等价于四舍五入）
//   return table[half + (余弦再 +0x800)]
// 上限：正弦 0x1FFF，余弦 0x27FF——余弦的相位偏移把下标顶到 0x27FF，这正是表
// 要留 0x2800 项而不是一个周期 0x2000... 的原因（见 sin_table.h）。
//
// 三个必须照搬的"毛刺"：
//   1. 角度被量化到 1/8192 圈（≈0.0439°），返回值只有 float 精度；
//   2. `_ftol` 向零截断 + 折半也向零截断，使 f(-x) 与 -f(x) **不相等**
//      （河流生成器 0x59D510 每步都做 x += Cos(θ) / y -= Sin(θ)，
//       这份不对称会真真切切改变河道走向）；
//   3. sin(π)/sin(2π) 那两项存的是 ~1e-16 的 x87 `fsin` 归约残渣而非 0。
// 消费者：河流生成器 0x59D510（MapType 3/4 支路）。
double Sin(double x);
double Cos(double x);

// 0x4CADE0：查表反正切。**不是** std::atan。全 exe 511 处调用点，
// RMG 这边的消费者是角度扩张 0x5A08D0（配 `if (dx < 0) a += π` 手写成 atan2）。
//
//   float xf = (float)x;                     // fst dword，向零截断，只用于取符号
//   int   k  = abs(_ftol(x / 0.024413354694843292f));   // 用**未窄化**的 x 求下标
//   r = (k >= 0x1001) ? 1.5707963705062866f : atan_table[k];
//   return (xf < 0.0f) ? -r : r;
//
// 三个必须照搬的"毛刺"：
//   1. 自变量被量化到 0.0244133547 的整数倍（约 1.4°，而且是**线性**分档——
//      x 小的时候精度奇高，x 大的时候一档能跨好几度）；
//   2. `k >= 0x1001` 那条出口给的是 float π/2 = 1.5707964，而 table[0x1000]
//      只有 1.5607948——**|x| 跨过约 100 时结果会跳 0.01 弧度**，不是连续的；
//   3. `abs` 排在 `_ftol` 之后，所以这个函数**是**严格奇对称的
//      （与 Sin/Cos 正相反，那两个因为 abs 缺席而不对称）。
//      唯一例外：|x| < 2^-149 时窄化成 ±0.0，符号丢失，恒返回 +0。
double Atan(double x);

// 原版用 x87 `fyl2x` 求自然对数（`fldln2; fld s; fyl2x`），全局 FPU 为 PC=53/RC=chop。
//
// 语义定为「**精确 ln 向零截断到 double**」，由 dd_log.h 的双双精度实现给出，
// **所有架构（含 x86）走同一条路**。理由见 docs/random-map/fast-math.md §四：
// 真 CPU 实测 20000 例中 `fyl2x` 有 7 例（0.035%）比该定义低 1 ulp——
// 是 `fyl2x` 自身内部近似不足以正确舍入，而非本定义有误。
// 既然 `fyl2x` 本就不是正确舍入的，钉死"本机 fyl2x 给什么就是什么"会让 x86 的
// 输出依赖具体 CPU 型号；改用唯一确定的数学定义，才能让各架构互相逐位一致。
// 这 1 ulp 不会传到下游：主消费者是 FastSqrt，它只取 float 尾数高 14 位
// （量化步长 2^-14），2^-52 的扰动翻转它的概率约 2^-38，实测 20000 例 0 次。
// 另一个消费者是 AngularGrow（0x5A0C40 `fldln2/fild k/fyl2x`），那里 ln(k) 里
// k 是小整数，最后还要过一次 _ftol；只有当中间值恰好压在整数边界上才会分岔。
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
