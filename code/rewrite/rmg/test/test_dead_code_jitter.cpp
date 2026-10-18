// 死代码复现 —— 0x5A2ED0 族「两锚点距离 + 随机抖动」黄金对拍
//
// 基准由 code/randmap/_golden_jitter.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_jitter_golden.txt（5 种子 × 5 坐标对，25 行）。
// 原版三副本（0x5A2ED0/0x5A4AE0/0x5A4FA0）逐位一致，golden 只记一个目标值。
// 比的是逐位 double（BitEq），不是近似相等——float32 位技巧 sqrt 的每一 ULP
// 都必须复刻到位。
#include "dead_code_jitter.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

::testing::AssertionResult BitEq(double got, double want) {
  std::uint64_t g, w;
  static_assert(sizeof(double) == sizeof(std::uint64_t), "double must be 64-bit");
  std::memcpy(&g, &got, sizeof(g));
  std::memcpy(&w, &want, sizeof(w));
  if (g == w) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "bits differ: got " << std::hex << g << " want " << w << std::dec
         << "  (got " << got << " want " << want << ")";
}

struct Case {
  std::uint32_t seed;
  std::int16_t ax, ay, bx, by;
  double want;
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_jitter_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    std::uint32_t s;
    long ax, ay, bx, by;
    double v;
    // seed=0 p1=(10,20) p2=(30,40) => 29.715425536136877 | ...
    if (std::sscanf(line.c_str(),
                    "seed=%u p1=(%ld,%ld) p2=(%ld,%ld) => %lf",
                    &s, &ax, &ay, &bx, &by, &v) != 6) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.ax = static_cast<std::int16_t>(ax);
    c.ay = static_cast<std::int16_t>(ay);
    c.bx = static_cast<std::int16_t>(bx);
    c.by = static_cast<std::int16_t>(by);
    c.want = v;
    cases.push_back(c);
  }
  return cases;
}

TEST(DeadCodeJitter, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    const double got = DeadCodeDistanceJitter(rng, c.ax, c.ay, c.bx, c.by);
    EXPECT_TRUE(BitEq(got, c.want))
        << "seed=" << c.seed << " p1=(" << c.ax << "," << c.ay << ") p2=("
        << c.bx << "," << c.by << ")";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
