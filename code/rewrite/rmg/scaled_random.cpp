#include "scaled_random.h"

#include <cfloat>
#include <cstdint>

#if defined(_MSC_VER)
#include <float.h>
#else
#include <cfenv>
#endif

namespace ra2::rmg {

namespace {
// 原版比例常数：n / (2^32 - 1)，与 exe 中的 double 字节逐位一致
inline double ScaleFor(int n) { return static_cast<double>(n) / 4294967295.0; }
}  // namespace

#if defined(_MSC_VER)
FpuChopScope::FpuChopScope() : saved_(_controlfp(0, 0)) {
  _controlfp(_RC_CHOP, _MCW_RC);
}
FpuChopScope::~FpuChopScope() { _controlfp(saved_, _MCW_RC); }
#else
FpuChopScope::FpuChopScope() : saved_(static_cast<unsigned>(std::fegetround())) {
  std::fesetround(FE_TOWARDZERO);
}
FpuChopScope::~FpuChopScope() { std::fesetround(static_cast<int>(saved_)); }
#endif

int PickBelow(Randomizer& rng, int n) {
  const double scale = ScaleFor(n);
  int v;
  do {
    v = static_cast<int>(static_cast<double>(rng.Random()) * scale);
  } while (v > n - 1);
  return v;
}

int PickBelowMul(Randomizer& rng, int limit) {
  // ⚠️ 上限是**零扩展**成 64 位再 fild 的：0x5A0C89 `mov [esp+0x44],ebx` 里
  // ebx 恒为 0，只有低半写的是上限本身。上限为正时与符号扩展没差别，
  // 但 AngularGrow（weight 为负 → 上限为负）会真的走到这条：-14 在这里
  // 变成 4294967282，抽出来的数于是几乎必然一次过关且大得离谱。
  const double scale = static_cast<double>(static_cast<std::uint32_t>(limit));
  const int hi = limit - 1;
  for (;;) {
    const std::uint32_t r = rng.Random();
    const double u =
        static_cast<double>(static_cast<std::int64_t>(r)) * scale * kInvUintMax;
    const int v = Ftol(u);
    // 0x59BCDA / 0x59AB26 的 cmp/ja 是**无符号**比较
    if (static_cast<unsigned>(v) <= static_cast<unsigned>(hi)) return v;
  }
}

int PickRange(Randomizer& rng, int lo, int hi) {
  // 0x59803E/0x59804A：span 与 base 都写进栈上 qword 的低半，**高半写 0**，
  // 再 fild qword。也就是说两者都按 **uint32** 读——lo 为负会变成天文数字。
  // 已知调用点都是 lo >= 0，但复刻照抄宽度，不替它"修正"。
  const double span =
      static_cast<double>(static_cast<std::uint32_t>(hi - lo + 1));
  const double base = static_cast<double>(static_cast<std::uint32_t>(lo));
  for (;;) {
    const std::uint32_t r = rng.Random();
    const double u =
        static_cast<double>(static_cast<std::int64_t>(r)) * span * kInvUintMax + base;
    const int v = Ftol(u);
    // 0x598089 是 ja —— **无符号**比较
    if (static_cast<unsigned>(v) <= static_cast<unsigned>(hi)) return v;
  }
}

int PickFrom1(Randomizer& rng, int n) {
  const double scale = ScaleFor(n);
  int v;
  do {
    v = static_cast<int>(static_cast<double>(rng.Random()) * scale + 1.0);
  } while (v > n);
  return v;
}

}  // namespace ra2::rmg
