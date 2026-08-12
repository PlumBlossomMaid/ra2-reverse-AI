#include "fast_math.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "atan_table.h"
#include "dd_log.h"
#include "sin_table.h"
#include "sqrt_table.h"

namespace ra2::rmg {
namespace {

constexpr double kInvUint32Max = 1.0 / 4294967295.0;  // exe 0x7ED898，位形 3DF0000000100000

// exe 0x8223B0（正弦用）与 0x8223B4（余弦用）——两个槽位，同一个 float 值。
// 它是 8192/π 截断到 float 的结果：真值 2607.5945876176133，存的是 2607.594482421875。
constexpr float kTrigScale = 2607.594482421875f;

float TableAt(int index) {
  float v;
  const std::uint32_t bits = kSinTableBits[index];
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

// `_ftol` @0x7C5F00 的真身是 `fistp qword`，返回 edx:eax；
// 这几个查表函数的调用点都只用 eax，所以取低 32 位。
// 溢出（|v| >= 2^63 或 NaN）时 x87 存的是 64 位「整数不定值」0x8000000000000000，
// **低 32 位恰好是 0**——所以越界的自变量会静默落到 table[0]，而不是越界读。
std::int32_t FtolLow32(double v) {
  if (!(v > -9.2233720368547758e18 && v < 9.2233720368547758e18)) return 0;
  const std::int64_t t = static_cast<std::int64_t>(v);        // 向零截断
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(
      static_cast<std::uint64_t>(t) & 0xFFFFFFFFull));
}

// 0x4CACBA / 0x4CAD0A
std::int32_t TrigIndex(double x) {
  return FtolLow32(x * static_cast<double>(kTrigScale));
}

// 0x4CACC1-0x4CACC7：`cdq / sub eax,edx / sar eax,1`
// = 先给负数补 1 再算术右移 = **向零截断**的除以 2（直接 >>1 会向下取整，差一格）。
std::int32_t HalfTowardZero(std::int32_t v) {
  const std::int64_t w = static_cast<std::int64_t>(v) + (v < 0 ? 1 : 0);
  return static_cast<std::int32_t>(w >> 1);
}

// 0x4CACC9-0x4CACDD / 0x4CAD19-0x4CAD2D：`and 0x80001FFF` 之后那段
// `dec / or 0xFFFFE000 / inc` + 两次 `jns` 的 13 位符号扩展再折回。
// 逐条走一遍会发现它**恒等于 v & 0x1FFF**（m==0 走 0 分支，m>0 先减 0x2000 再加回
// 0x2000），也就是说这十几字节是白写的。照抄是为了让"等价"这件事有代码为证，
// 而不是靠一句注释——把它删掉才需要举证。
float BitsToFloat(std::uint32_t bits) {
  float v;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

std::uint32_t FoldIndex(std::int32_t half) {
  std::uint32_t a = static_cast<std::uint32_t>(half) & 0x80001FFFu;
  if (a & 0x80000000u) {              // jns 不跳
    a = ((a - 1u) | 0xFFFFE000u) + 1u;
  }
  return a;
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
      kSqrtTable[mantissa >> 10] + (static_cast<std::uint32_t>(half + 0x7F) << 23);

  float result;
  std::memcpy(&result, &out, sizeof result);
  return result;
}

double Sin(double x) {
  const std::int32_t idx = TrigIndex(x);
  const bool odd = (idx & 1) != 0;            // 0x4CACBF/0x4CACC4：先存 al 再 and 1
  std::uint32_t a = FoldIndex(HalfTowardZero(idx));
  if (a & 0x80000000u) a += 0x2000u;          // 0x4CACD7 jns / 0x4CACD9
  std::int32_t k = static_cast<std::int32_t>(a);
  if (odd && k < 0x1FFF) ++k;                 // 0x4CACE2：到顶就不再补半步
  return TableAt(k);                          // fld dword → 加宽成 double，精确
}

double Cos(double x) {
  const std::int32_t idx = TrigIndex(x);
  const bool odd = (idx & 1) != 0;
  std::uint32_t a = FoldIndex(HalfTowardZero(idx));
  a += (a & 0x80000000u) ? 0x2800u : 0x800u;  // 0x4CAD27 jns / 0x4CAD29 / 0x4CAD30
  std::int32_t k = static_cast<std::int32_t>(a);
  if (odd && k < 0x27FF) ++k;                 // 0x4CAD39
  return TableAt(k);
}

double Atan(double x) {
  // 0x4CADE4 `fst dword [esp+4]`：把 st0 窄化成 float **存回栈上但不出栈**，
  // 后面只用它取符号；求下标用的仍是没被窄化的那个 double。
  const float narrowed = static_cast<float>(x);

  // 0x4CADE8 `fdiv dword [0x8650B8]` + 0x4CADEE `_ftol`（向零截断）
  const std::int32_t raw = FtolLow32(x / static_cast<double>(BitsToFloat(kAtanStepBits)));
  // 0x4CADF3 `cdq / xor eax,edx / sub eax,edx`：整数绝对值（按补码，无饱和）
  const std::uint32_t k =
      (raw < 0) ? (0u - static_cast<std::uint32_t>(raw)) : static_cast<std::uint32_t>(raw);

  // 0x4CADF8 `cmp eax,0x1001 / jge` —— **有符号**比较。
  // `abs` 唯一可能产出的负数是 INT32_MIN，而 `_ftol` 的低 32 位溢出时给的是 0，
  // 所以 k 永远落在 [0, 2^31)，这条判断等价于无符号比较。
  const float r = (static_cast<std::int32_t>(k) >= kAtanTableSize)
                      ? BitsToFloat(kAtanHalfPiBits)
                      : BitsToFloat(kAtanTableBits[k]);

  // 0x4CAE0E-0x4CAE1F：拿窄化后的 float 与 0.0f 比，C0 置位（即 x < 0）才 fchs
  return (narrowed < 0.0f) ? -static_cast<double>(r) : static_cast<double>(r);
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
  const double sqrt_in = (-l - l) / s;
  const double f = FastSqrt(sqrt_in);   // sqrt(-2*ln(s)/s)

  has_cached_ = true;
  cached_ = f * v;                   // 第二个偏差留到下次调用
  return f * u;
}

}  // namespace ra2::rmg
