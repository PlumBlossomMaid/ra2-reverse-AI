#include "fast_math.h"

#include <cmath>
#include <cstring>

#include "dd_log.h"
#include "sqrt_table.h"

namespace ra2::rmg {
namespace {

constexpr double kInvUint32Max = 1.0 / 4294967295.0;  // exe 0x7ED898，位形 3DF0000000100000

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
      kSqrtTable[mantissa >> 10] + (static_cast<std::uint32_t>(half + 0x7F) << 23);

  float result;
  std::memcpy(&result, &out, sizeof result);
  return result;
}

// 所有架构统一走双双精度实现——它给出的是「精确 ln 向零截断」这个唯一确定的数学值。
// 不用本机 fyl2x 的理由见头文件与 docs/random-map/fast-math.md §四。
double ChopLog(double s) { return dd::ChopLogPortable(s); }

#if defined(_M_IX86) && !defined(_M_ARM)
// 原版 0x598126: fldln2 / fxch / fyl2x。仅作测试取证用，产品路径不走这里。
double ChopLogX87(double s) {
  double result;
  __asm {
    fldln2
    fld  qword ptr [s]
    fyl2x
    fstp qword ptr [result]
  }
  return result;
}
#endif

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
