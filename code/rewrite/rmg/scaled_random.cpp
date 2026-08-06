#include "scaled_random.h"

#include <cfloat>

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

int PickFrom1(Randomizer& rng, int n) {
  const double scale = ScaleFor(n);
  int v;
  do {
    v = static_cast<int>(static_cast<double>(rng.Random()) * scale + 1.0);
  } while (v > n);
  return v;
}

}  // namespace ra2::rmg
