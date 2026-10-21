// 死代码复现 —— 0x5A5020 编排树根 A 黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a5020.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a5020_golden.txt（3 种子 × 4 用例 = 12 行）。
// 每例：给定 this60（内层完成数上限 = _ftol((this60+0.32)*3.0)）与 side，
// 函数数组初始化 + 外层循环（上限 10）+ 内层选点（PickRange/判空/谓词/邻域）
// + 概率判定 → magic 五连调（NormalDeviate 打桩 0.2 一次通过）。
// 对拍：
//   1. 数组初始化 arr1/arr2（101 项 + 8 项首末）== golden；
//   2. 子调用序列（五连调 + 虚函数调用）== golden calls；
//   3. 调用后下一次 Random() == golden next（随机消耗次数与路径）。
//
// ★探针打桩语义（与测试同源）：
//   work 数组 side² 项 {x=i%side, y=i//side, +0x45=1}——只有 v=0 是空格
//   （WorkEmpty 命中 → PickRange 重抽）；
//   GetCellAt → cell tile=0 → 0x486380 谓词通过（CellPass 恒 true）；
//   0x4863D0 → 0（NeighborPass 恒 false，邻域扫描穿透）；
//   0x5980C0 → 0.2（NormalDeviate 桩值，接受窗口 [100,2000] 一次通过）；
//   0x5A5360 → 非 0（五连调继续）；虚函数调用 → 记录 "vfn"。
#include "dead_code_5a5020.h"

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
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a5020_golden.txt";
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

// 探针打桩语义的 world（记录调用序列 + 模拟 work/cell/ND 桩）
class GoldenWorld : public DeadCode5A5020World {
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
  bool WorkFlag45(std::uint32_t /*idx*/) override { return true; }  // 探针全 1

  bool CellPass(int /*x*/, int /*y*/) override { return true; }  // tile=0 通过
  bool NeighborPass(int /*x*/, int /*y*/) override { return false; }  // 打桩 0
  double NormalDeviate() override { return 0.2; }  // 打桩值（一次通过）

  bool Sub5A89D0() override { seq.push_back("89D0"); return true; }
  void Sub5A82E0() override { seq.push_back("82E0"); }
  void Sub5A8720() override { seq.push_back("8720"); }
  bool Sub5A5360() override { seq.push_back("5360"); return true; }  // 非 0 继续
  void Sub5A5B50() override { seq.push_back("5B50"); }
  void Sub5A6510() override { seq.push_back("6510"); }
  void Sub5A6920() override { seq.push_back("6920"); }
  void Sub5A78D0() override { seq.push_back("78D0"); }
  void VfnCall() override { seq.push_back("vfn"); }

  std::int32_t side_ = 0;
  std::vector<bool> empty_;
  std::vector<std::string> seq;
};

TEST(DeadCode5A5020, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    GoldenWorld world(c.side);
    DeadCode5A5020Context ctx;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.this60 = c.this60;
    ctx.side = c.side;
    ctx.map_w = 100;  // 探针灌 [0x87F8DC]=100
    ctx.map_h = 100;  // 探针灌 [0x87F8E0]=100
    ctx.src1 = 0x11111111;
    ctx.src2 = 0x22222222;
    DeadCode5A5020(ctx);
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