// 死代码复现 —— 0x5A80B0 编排树根 B 黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a80b0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a80b0_golden.txt（3 种子 × 4 用例 = 12 行）。
// 每例：给定 this60（内层完成数上限 = _ftol((this60+0.49)*2)）与 side，
// 函数数组初始化 + 外层循环（上限 10）+ 内层选点（PickBelowMul(side²) 形态
// 判空/0x486380 谓词）+ 菱形校验 0x5A7250(x-4,y-4,8,8,0,0) + 0x578460
// 可用区判定 + **无条件三连调**（★无概率判定——与根 A 0x5A5020 关键差异）。
// 对拍：
//   1. 数组初始化 arr1/arr2（101 项 + 8 项首末）== golden；
//   2. 子调用序列（菱形+可用区+三连调）== golden calls；
//   3. 调用后下一次 Random() == golden next（随机消耗次数与路径）。
//
// ★探针打桩语义（与测试同源）：
//   work 数组 side² 项 {x=i%side, y=i//side, +0x45=0}——只有 v=0 是空格
//   （WorkEmpty 命中 → 重抽）；★f45=0 → 每轮走菱形校验 + 可用区判定 +
//   三连调（全链覆盖，golden calls 每轮 5 连调实证）；
//   GetCellAt → cell tile=0 → 0x486380 谓词通过（CellPass 恒 true）；
//   0x5A7250 → 1（DiamondPass 恒 true）；0x578460 → 1（UsablePass 恒 true）；
//   0x5A89D0 → 1（继续三连调）；0x5A82E0/0x5A8720 → 记录序列。
#include "dead_code_5a80b0.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct Case {
  std::uint32_t seed;
  std::int32_t this60;
  std::int32_t side;
  // 数组初始化首末（探针灌 [0xABC1D4]=0x11111111、[0xABBEC0]=0x22222222）
  std::uint32_t a1_first = 0, a1_last = 0;
  std::uint32_t a2_first = 0, a2_last = 0;
  std::string calls;  // "none" 或逗号分隔调用序列
  std::uint32_t next;
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a80b0_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, n;
    int t, side, a1f, a1l, a2f, a2l;
    char calls[2048];
    // seed=%u this60=%d side=%d arr1=(%X,%X) arr2=(%X,%X) => calls=%s next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u this60=%d side=%d arr1=(%X,%X) arr2=(%X,%X) "
                    "=> calls=%2047s next=0x%X",
                    &s, &t, &side, &a1f, &a1l, &a2f, &a2l, calls, &n) != 9) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.this60 = t;
    c.side = side;
    c.a1_first = static_cast<std::uint32_t>(a1f);
    c.a1_last = static_cast<std::uint32_t>(a1l);
    c.a2_first = static_cast<std::uint32_t>(a2f);
    c.a2_last = static_cast<std::uint32_t>(a2l);
    c.calls = calls;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// 探针打桩语义的 world（记录调用序列 + 模拟 work/cell 桩）
class GoldenWorld : public DeadCode5A80B0World {
 public:
  explicit GoldenWorld(std::int32_t side) : side_(side) {
    const std::uint32_t n = static_cast<std::uint32_t>(side) * side;
    empty_.assign(n, false);
    for (std::uint32_t i = 0; i < n; ++i) {
      const std::uint32_t x = i % side_;
      const std::uint32_t y = i / side_;
      if (x == 0 && y == 0) empty_[i] = true;  // work[0] = (0,0) 空格
    }
  }

  std::uint32_t WorkCoord(std::uint32_t v) override {
    const std::uint32_t x = v % static_cast<std::uint32_t>(side_);
    const std::uint32_t y = v / static_cast<std::uint32_t>(side_);
    return (y << 16) | x;  // work[v] 首 dword 坐标打包
  }
  bool WorkEmpty(std::uint32_t v) override { return empty_[v]; }
  bool WorkFlag45(std::uint32_t /*idx*/) override { return false; }  // 探针全 0

  bool CellPass(int /*x*/, int /*y*/) override { return true; }  // tile=0 通过
  bool DiamondPass(int /*x*/, int /*y*/) override {
    seq.push_back("7250");
    return true;  // 打桩 1 = 菱形通过
  }
  bool UsablePass(int /*x*/, int /*y*/) override {
    seq.push_back("78460");
    return true;  // 打桩 1 = 可用区通过
  }

  bool Sub5A89D0() override { seq.push_back("89D0"); return true; }
  void Sub5A82E0() override { seq.push_back("82E0"); }
  void Sub5A8720() override { seq.push_back("8720"); }

  std::int32_t side_ = 0;
  std::vector<bool> empty_;
  std::vector<std::string> seq;
};

TEST(DeadCode5A80B0, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    GoldenWorld world(c.side);
    DeadCode5A80B0Context ctx;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.this60 = c.this60;
    ctx.side = c.side;
    ctx.src1 = 0x11111111;
    ctx.src2 = 0x22222222;
    DeadCode5A80B0(ctx);
    // 1. 数组初始化（12B 步长写首 dword = 源+i）
    EXPECT_EQ(ctx.arr1[0], c.a1_first) << "seed=" << c.seed;
    EXPECT_EQ(ctx.arr1[100], c.a1_last) << "seed=" << c.seed;
    EXPECT_EQ(ctx.arr2[0], c.a2_first) << "seed=" << c.seed;
    EXPECT_EQ(ctx.arr2[7], c.a2_last) << "seed=" << c.seed;
    // 2. 子调用序列
    std::string got;
    for (std::size_t i = 0; i < world.seq.size(); ++i) {
      if (i) got += ",";
      got += world.seq[i];
    }
    if (got.empty()) got = "none";
    EXPECT_EQ(got, c.calls) << "seed=" << c.seed << " this60=" << c.this60
                            << " side=" << c.side;
    // 3. next（随机消耗次数与路径）
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed
                           << " this60=" << c.this60 << " side=" << c.side;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2