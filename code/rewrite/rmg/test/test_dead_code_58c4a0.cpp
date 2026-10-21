// 死代码复现 —— 0x58C4A0 随机取「已占用且类型匹配且谓词通过」格子黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_58c4a0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_58c4a0_golden.txt（5 种子 × 11 用例 = 55 行）。
// 每例：给定 seed/side/this8/tile/empty/mism/f44z，函数从 work 数组拒绝采样
// 取「已占用且 [0x38] 匹配 this8 且 [0x44]!=0 且 GetCellAt 谓词通过」的格。
// 对拍：
//   1. 返回 dword（成功 work[v] 首 dword / 100 次上限后 0）；
//   2. 下一次 Random() 的值 == golden next（校验随机消耗次数一致——
//      拒绝采样、各重抽路径、100 次上限的消耗都要逐位对上）。
#include "dead_code_58c4a0.h"

#include <cstdint>
#include <cstdio>
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

struct Case {
  std::uint32_t seed;
  int side;
  std::uint32_t this8;
  int tile;      // GetCellAt 返回 cell 的 +0x38（0 → 0x486380 通过）
  int empty;     // work 前 empty 项为 (0,0) 空格子
  int mism;      // 之后 mism 项 [+0x38] 不匹配
  int f44z;      // 之后 f44z 项 [+0x44]==0
  std::uint32_t out;     // 期望返回 dword
  std::uint32_t next;    // 调用后下一次 Random() 值
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_58c4a0_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, t8, o, n;
    int side, tile, emp, mism, f44z;
    // seed=%u side=%d this8=%u tile=%d empty=%d mism=%d f44z=%d => out=0x%X next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u side=%d this8=%u tile=%d empty=%d mism=%d f44z=%d => out=0x%X next=0x%X",
                    &s, &side, &t8, &tile, &emp, &mism, &f44z, &o, &n) != 9) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.side = side;
    c.this8 = t8;
    c.tile = tile;
    c.empty = emp;
    c.mism = mism;
    c.f44z = f44z;
    c.out = o;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// work 构造与探针 make_work（.qwen/tmp/_emu_deadcode_58c4a0.py）一致：
// 前 empty 项为 (0,0) 空格子；之后 mism 项坐标 (i,i+1) 但 [+0x38]=this8^0x55；
// 之后 f44z 项坐标 (i,i+1) 且 [+0x38]=this8 但 [+0x44]=0；其余全通过项。
std::vector<DeadCode58C4A0Rec> MakeWork(int side, std::uint32_t this8,
                                        int empty, int mism, int f44z) {
  const int n = side * side;
  std::vector<DeadCode58C4A0Rec> w;
  w.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    DeadCode58C4A0Rec r;
    if (i < empty) {
      r.coord = 0;
      r.field38 = this8;
      r.field44 = 1;
    } else if (i < empty + mism) {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = this8 ^ 0x55u;  // 类型不匹配
      r.field44 = 1;
    } else if (i < empty + mism + f44z) {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = this8;          // 类型匹配
      r.field44 = 0;              // ★field44==0 重抽
    } else {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = this8;          // 类型匹配
      r.field44 = 1;              // 可通过
    }
    w.push_back(r);
  }
  return w;
}

// 0x486380 谓词：cell+0x38 == 0xFFFF || == 0 → 通过（真）。
// golden 探针统一装配 CELL_OBJ.tile：tile==0 → 通过；tile==0x40 → 拦截。
class GoldenWorld : public DeadCode58C4A0World {
 public:
  explicit GoldenWorld(int tile) : tile_(tile) {}
  Randomizer& Rng() override { return *rng_; }
  bool CellPass(int x, int y) override {
    (void)x;
    (void)y;
    return tile_ == 0xFFFF || tile_ == 0;  // 0x486380 真身语义
  }
  Randomizer* rng_ = nullptr;
  int tile_;
};

TEST(DeadCode58C4A0, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    GoldenWorld world(c.tile);
    world.rng_ = &rng;
    DeadCode58C4A0Context ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.side = c.side;
    ctx.this8 = c.this8;
    ctx.work = MakeWork(c.side, c.this8, c.empty, c.mism, c.f44z);
    const std::uint32_t got = DeadCode58C4A0(ctx);
    EXPECT_EQ(got, c.out) << "seed=" << c.seed << " side=" << c.side
                          << " this8=" << c.this8 << " tile=" << c.tile
                          << " empty=" << c.empty << " mism=" << c.mism
                          << " f44z=" << c.f44z;
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed
                           << " side=" << c.side << " this8=" << c.this8
                           << " tile=" << c.tile << " empty=" << c.empty
                           << " mism=" << c.mism << " f44z=" << c.f44z;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2