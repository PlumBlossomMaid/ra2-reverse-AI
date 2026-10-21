// 死代码复现 —— 0x5A6BA0 锚点邻域扫描器黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a6ba0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a6ba0_golden.txt（3 种子 × 8 用例 = 24 行）。
// 每例：给定锚点 (X,Y) 与命中模式 pat，函数扫描 [X-7,X+7)×[Y-7,Y+7) 邻域，
// 逐格 GetCellAt→0x4863D0 谓词（golden 打桩 cell+0x38==k）；谓词真 → 返 0
// （邻域内有可用格，提前退出）；全假 → 返 1。
// 对拍：
//   1. 返回值（0/1）；
//   2. CellPass 调用次数 == golden calls（验证提前退出路径与扫描范围）；
//   3. 调用后下一次 Random() == golden next（验证零取数点——函数不消耗 RNG）。
#include "dead_code_5a6ba0.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
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

// 命中模式（邻域内偏移，0-based：ix 对应外层 X、iy 对应内层 Y）
//   none=无命中 / first=(0,0) / last=(13,13) / center=(7,7) /
//   ex=(13,0) / ey=(0,13) / corner=(0,0)+(13,13) / mid2=(3,4)+(9,10)
struct Pattern {
  std::string name;
  std::vector<std::pair<int, int>> cells;
};

std::vector<Pattern> MakePatterns() {
  return {
      {"none", {}},
      {"first", {{0, 0}}},
      {"last", {{13, 13}}},
      {"center", {{7, 7}}},
      {"ex", {{13, 0}}},
      {"ey", {{0, 13}}},
      {"corner", {{0, 0}, {13, 13}}},
      {"mid2", {{3, 4}, {9, 10}}},
  };
}

struct Case {
  std::uint32_t seed;
  int x;
  int y;
  std::uint32_t k;      // 打桩谓词：cell+0x38 == k 通过
  std::string pat;      // 命中模式名
  int out;              // 期望返回值（0=有可用格 / 1=全不可用）
  int calls;            // 期望 CellPass 调用次数
  std::uint32_t next;   // 调用后下一次 Random() 值（零取数点验证）
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a6ba0_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, k, o, n;
    int X, Y, cl;
    char pat[32];
    // seed=%u X=%d Y=%d k=%u pat=%s => out=%u calls=%d next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u X=%d Y=%d k=%u pat=%31s => out=%u calls=%d next=0x%X",
                    &s, &X, &Y, &k, pat, &o, &cl, &n) != 8) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.x = X;
    c.y = Y;
    c.k = k;
    c.pat = pat;
    c.out = static_cast<int>(o);
    c.calls = cl;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// 0x4863D0 打桩：cell+0x38 == k → 通过（真）。golden 按坐标装配 PASS/FAIL
// cell 对象（命中格 +0x38=k、其余 +0x38=k^0x55）。C++ 侧直接按命中模式
// 判定该格是否「谓词通过」，并统计调用次数。
class GoldenWorld : public DeadCode5A6BA0World {
 public:
  GoldenWorld(const std::vector<std::pair<int, int>>& hit, int X, int Y)
      : hit_(hit), X_(X), Y_(Y) {}
  bool CellPass(int x, int y) override {
    ++calls_;
    // 命中格 = 锚点 + 偏移（ix → x = X-7+ix）
    for (const auto& h : hit_) {
      if (x == X_ - 7 + h.first && y == Y_ - 7 + h.second) {
        return true;
      }
    }
    return false;
  }
  int calls_ = 0;
  const std::vector<std::pair<int, int>>& hit_;
  int X_;
  int Y_;
};

TEST(DeadCode5A6BA0, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const auto patterns = MakePatterns();
  for (const auto& c : cases) {
    const Pattern* p = nullptr;
    for (const auto& pp : patterns) {
      if (pp.name == c.pat) p = &pp;
    }
    ASSERT_NE(p, nullptr) << "unknown pattern: " << c.pat;
    Randomizer rng(c.seed);
    GoldenWorld world(p->cells, c.x, c.y);
    DeadCode5A6BA0Context ctx;
    ctx.world = &world;
    ctx.x = static_cast<std::int16_t>(c.x);
    ctx.y = static_cast<std::int16_t>(c.y);
    const int got = DeadCode5A6BA0(ctx);
    EXPECT_EQ(got, c.out) << "seed=" << c.seed << " X=" << c.x << " Y=" << c.y
                          << " k=" << c.k << " pat=" << c.pat;
    EXPECT_EQ(world.calls_, c.calls) << "calls seed=" << c.seed
                                     << " X=" << c.x << " Y=" << c.y
                                     << " k=" << c.k << " pat=" << c.pat;
    // 零取数点：函数全程不调 Randomizer，调用后下一次 Random() 值
    // 应等于同种子首采值（golden next）。
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed << " X=" << c.x
                           << " Y=" << c.y << " k=" << c.k << " pat=" << c.pat;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2