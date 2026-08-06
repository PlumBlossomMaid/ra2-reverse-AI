#include "fast_math.h"

#include <cmath>
#include <cstring>

namespace ra2::rmg {
namespace {

constexpr double kInvUint32Max = 1.0 / 4294967295.0;  // exe 0x7ED898，位形 3DF0000000100000

// 64 位整数开方（逐位法），用于重算查表项——全程整数，不受浮点舍入模式影响
std::uint64_t IntSqrt(std::uint64_t n) {
  std::uint64_t root = 0;
  std::uint64_t bit = std::uint64_t(1) << 62;
  while (bit > n) bit >>= 2;
  while (bit != 0) {
    if (n >= root + bit) {
      n -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return root;
}

// 原版 0x8650BC 处的 16384 项表。
// 索引 i 代表尾数区间 [i*1024, (i+1)*1024) 的下界 m：
//   m <  2^23 -> 被开方数 v = 1 + m/2^23      （偶指数）
//   m >= 2^23 -> 被开方数 v = 2m/2^23         （奇指数，原版靠 or 0x800000 标记）
// 表项 = floor(sqrt(v) * 2^23) - 2^23，而 sqrt(v)*2^23 = sqrt(v * 2^46) 恰为整数开方。
struct SqrtTable {
  static constexpr int kSize = 16384;
  std::uint32_t entry[kSize];

  SqrtTable() {
    for (int i = 0; i < kSize; ++i) {
      const std::uint64_t m = std::uint64_t(i) * 1024;
      const std::uint64_t n = (m >= (std::uint64_t(1) << 23))
                                  ? (2 * m) << 23
                                  : ((std::uint64_t(1) << 23) + m) << 23;
      entry[i] = static_cast<std::uint32_t>(IntSqrt(n) - (std::uint64_t(1) << 23));
    }
  }
};

const SqrtTable& Table() {
  static const SqrtTable table;
  return table;
}

}  // namespace

double FastSqrt(double x) {
  // 0x4CAC44 fcom 0.0f：等于 0 直接返回 0
  if (x == 0.0) return 0.0;
  // 0x4CAC67 fmul -1.0：负数取绝对值（在 double 上做，之后才转 float）
  if (x < 0.0) x = x * -1.0;

  // 0x4CAC6D fstp dword：double -> float，受当前舍入模式（原版为向零截断）
  const float f = static_cast<float>(x);
  std::uint32_t bits;
  std::memcpy(&bits, &f, sizeof bits);

  std::uint32_t mantissa = bits & 0x7FFFFF;
  // 0x4CAC7D shr 23 / sub 0x7F —— 以 16 位宽做算术右移（原版 sar ax,1）
  const std::int16_t exponent = static_cast<std::int16_t>((bits >> 23) - 0x7F);
  if (exponent & 1) mantissa |= 0x800000;

  const std::int32_t half = static_cast<std::int16_t>(exponent >> 1);
  const std::uint32_t out =
      Table().entry[mantissa >> 10] + (static_cast<std::uint32_t>(half + 0x7F) << 23);

  float result;
  std::memcpy(&result, &out, sizeof result);
  return result;
}

double ChopLog(double s) {
#if defined(_M_IX86) && !defined(_M_ARM)
  // 原版 0x598126: fldln2 / fxch / fyl2x —— 直接用同一条 x87 指令，逐位等价
  double result;
  __asm {
    fldln2
    fld  qword ptr [s]
    fyl2x
    fstp qword ptr [result]
  }
  return result;
#else
  // 非 x86 目标没有 fyl2x。std::log 是就近舍入，与原版的向零截断在约半数取值上
  // 差 1 ulp——此路径**未验证逐位一致**，仅供非复刻用途。
  return std::log(s);
#endif
}

double UniformUnit(Randomizer& rng) {
  // 0x598019 fild qword：uint32 先补成 64 位有符号再转 double（x87 无无符号转换）
  return static_cast<double>(static_cast<std::int64_t>(rng.Random())) * kInvUint32Max;
}

double NormalDeviate::operator()() {
  if (has_cached_) {
    has_cached_ = false;
    return cached_;
  }

  double u, v, s;
  for (;;) {
    u = UniformUnit(*rng_);
    u = u + u - 1.0;                 // 0x5980E1 fadd st,st / fsub 1.0
    v = UniformUnit(*rng_);
    v = v + v - 1.0;
    s = v * v + u * u;               // 0x5980FE 先 v*v，再加 u*u
    if (s < 1.0 && s != 0.0) break;  // 落在单位圆外或正中心则整轮重抽
  }

  const double l = ChopLog(s);
  const double f = FastSqrt((-l - l) / s);   // sqrt(-2*ln(s)/s)

  has_cached_ = true;
  cached_ = f * v;                   // 第二个偏差留到下次调用
  return f * u;
}

}  // namespace ra2::rmg
