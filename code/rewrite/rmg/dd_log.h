// 「向零截断自然对数」——**所有架构**上 ChopLog 的唯一实现
//
// 原版求 ln 用的是 x87 `fyl2x`（0x598126），全局 FPU 为 PC=53/RC=chop。
// 但真 CPU 实测（20000 例）表明 `fyl2x` **本身就不是正确舍入的**：
// 有 7 例（0.035%）比精确截断低 1 ulp，全部落在精确值距截断边界不足 0.4 个
// 80 位 ulp 处。跟着本机指令走等于把输出绑死在具体 CPU 实现上，
// 因此这里改为实现唯一确定的数学语义：**精确 ln 向零截断到 double**。
// x86 也走这条路，`ChopLogX87` 仅在测试中保留作取证。
// 完整经过与"这 1 ulp 为何不影响地图"的论证见 docs/random-map/fast-math.md §四。
//
// std::log 是就近舍入、约半数取值差 1 ulp，不能用。
// 本实现与任意精度模型（Decimal 90 位 + Fraction 精确截断）**20000/20000 逐位一致**。
//
// 这里用双双精度（double-double，约 106 位有效）算出 ln，再手工按符号截断：
//   ln = hi + lo，|lo| <= ulp(hi)/2；hi 与 lo 异号说明 hi 越过了零方向，退一个 ulp。
//
// 注意：双双精度的 TwoSum / Dekker 拆分**要求就近舍入**才精确，
// 因此 ChopLog 内部会临时把舍入模式切回 FE_TONEAREST，算完再恢复。
#pragma once

#include <cfenv>
#include <cmath>

// 本文件会在运行期切换舍入模式，必须禁止编译器跨越切换点重排/常量折叠浮点运算
#if defined(_MSC_VER)
#pragma fenv_access(on)
#elif defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif

namespace ra2::rmg::dd {

struct Dd {
  double hi;
  double lo;
};

inline Dd QuickTwoSum(double a, double b) {  // 要求 |a| >= |b|
  const double s = a + b;
  return {s, b - (s - a)};
}

inline Dd TwoSum(double a, double b) {
  const double s = a + b;
  const double bb = s - a;
  return {s, (a - (s - bb)) + (b - bb)};
}

inline Dd TwoProd(double a, double b) {
  constexpr double kSplitter = 134217729.0;  // 2^27 + 1
  const double p = a * b;
  const double ta = kSplitter * a;
  const double ahi = ta - (ta - a);
  const double alo = a - ahi;
  const double tb = kSplitter * b;
  const double bhi = tb - (tb - b);
  const double blo = b - bhi;
  const double err = ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
  return {p, err};
}

inline Dd Add(Dd a, Dd b) {
  Dd s = TwoSum(a.hi, b.hi);
  const Dd t = TwoSum(a.lo, b.lo);
  s.lo += t.hi;
  s = QuickTwoSum(s.hi, s.lo);
  s.lo += t.lo;
  return QuickTwoSum(s.hi, s.lo);
}

inline Dd Mul(Dd a, Dd b) {
  Dd p = TwoProd(a.hi, b.hi);
  p.lo += a.hi * b.lo + a.lo * b.hi;
  return QuickTwoSum(p.hi, p.lo);
}

inline Dd Div(Dd a, Dd b) {
  const double q1 = a.hi / b.hi;
  Dd r = Add(a, Mul(Dd{-q1, 0.0}, b));
  const double q2 = r.hi / b.hi;
  r = Add(r, Mul(Dd{-q2, 0.0}, b));
  const double q3 = r.hi / b.hi;
  return Add(QuickTwoSum(q1, q2), Dd{q3, 0.0});
}

// ln(s)，双双精度。s > 0。
//   s = m * 2^e，把 m 规约到 [1/√2, √2)，令 z = (m-1)/(m+1)，则
//   ln(m) = 2*atanh(z) = 2*(z + z³/3 + z⁵/5 + ...)
//   |z| <= 0.1716，z² <= 0.0295，逐项衰减约 34 倍，22 项即可达到 2^-107
inline Dd LogDd(double s) {
  constexpr Dd kLn2 = {6.9314718055994530942e-01, 2.3190468138462996e-17};
  constexpr int kTerms = 22;

  int e = 0;
  double m = std::frexp(s, &e);            // m ∈ [0.5, 1)
  if (m < 0.70710678118654752440) {
    m *= 2.0;
    e -= 1;
  }

  // m ∈ [0.7071, 1.4142)，故 m-1 与 m+1 都无舍入误差（Sterbenz / 指数不超界）
  const Dd z = Div(Dd{m - 1.0, 0.0}, TwoSum(m, 1.0));
  const Dd z2 = Mul(z, z);

  Dd acc{0.0, 0.0};
  for (int k = kTerms; k >= 0; --k) {
    // 系数 1/(2k+1) 本身不是精确 double，必须也用双双精度，否则只能保到 2^-59
    acc = Add(Mul(acc, z2), Div(Dd{1.0, 0.0}, Dd{static_cast<double>(2 * k + 1), 0.0}));
  }

  return Add(Mul(Dd{static_cast<double>(e), 0.0}, kLn2),
             Mul(Dd{2.0, 0.0}, Mul(z, acc)));
}

// 精确 ln 向零截断到 double
inline double ChopLogPortable(double s) {
  const int saved = std::fegetround();
  std::fesetround(FE_TONEAREST);           // 双双精度算法要求就近舍入
  const Dd l = LogDd(s);
  std::fesetround(saved);

  // 向零截断：如果精确值（hi+lo）的绝对值 > |hi|（即 lo 与 hi 同号），
  // 则 hi 已经是向零截断的结果（|hi| < |精确值|）。
  // 如果精确值的绝对值 < |hi|（即 lo 与 hi 异号），则 hi 在远离零的一侧，
  // 需要退一个 ULP。
  // 特殊情况 lo == 0：精确值 = hi，直接返回。
  if (l.lo != 0.0) {
    const bool hi_neg = l.hi < 0.0;
    const bool lo_neg = l.lo < 0.0;
    if (hi_neg != lo_neg) {
      // 异号：|hi| > |精确值|，hi 在远离零一侧，退一个 ULP
      return std::nextafter(l.hi, 0.0);
    }
    // 同号：|hi| < |精确值|，hi 已在靠近零一侧，直接返回
  }
  return l.hi;
}

}  // namespace ra2::rmg::dd
