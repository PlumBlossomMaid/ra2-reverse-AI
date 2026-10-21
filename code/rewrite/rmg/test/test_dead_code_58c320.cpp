// 死代码复现 —— 0x58C320 随机取「已占用且类型匹配」格子黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_58c320.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_58c320_golden.txt（5 种子 × 9 用例 = 45 行）。
// 每例：给定 seed/side/this8/empty/mism，函数从 work 数组拒绝采样取
// 「已占用且 [0x38] 匹配 this8」的格。对拍：
//   1. 返回 dword（work[v] 首 dword = x|(y<<16)）逐位一致；
//   2. 下一次 Random() 的值 == golden next（校验随机消耗次数一致）。
#include "dead_code_58c320.h"

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
  int empty;             // work 前 empty 项为 (0,0) 空格子
  int mism;              // 之后 mism 项 [+0x38] 不匹配
  std::uint32_t out;     // 期望返回 dword
  std::uint32_t next;    // 调用后下一次 Random() 值
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_58c320_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, t8, o, n;
    int side, emp, mism;
    // seed=%u side=%d this8=%u empty=%d mism=%d => out=0x%X next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u side=%d this8=%u empty=%d mism=%d => out=0x%X next=0x%X",
                    &s, &side, &t8, &emp, &mism, &o, &n) != 7) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.side = side;
    c.this8 = t8;
    c.empty = emp;
    c.mism = mism;
    c.out = o;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// work 构造与探针 make_work（.qwen/tmp/_emu_deadcode_58c320.py）一致：
// 前 empty 项为 (0,0)；之后 mism 项坐标 (i,i+1) 但 [+0x38]=this8^0x55；
// 其余坐标 (i,i+1) 且 [+0x38]=this8 匹配。
std::vector<DeadCode58C320Rec> MakeWork(int side, std::uint32_t this8,
                                        int empty, int mism) {
  const int n = side * side;
  std::vector<DeadCode58C320Rec> w;
  w.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    DeadCode58C320Rec r;
    if (i < empty) {
      r.coord = 0;
      r.field38 = this8;
    } else if (i < empty + mism) {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = this8 ^ 0x55u;  // 不匹配
    } else {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = this8;          // 匹配
    }
    w.push_back(r);
  }
  return w;
}

TEST(DeadCode58C320, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    DeadCode58C320Context ctx;
    ctx.rng = &rng;
    ctx.side = c.side;
    ctx.this8 = c.this8;
    ctx.work = MakeWork(c.side, c.this8, c.empty, c.mism);
    const std::uint32_t got = DeadCode58C320(ctx);
    EXPECT_EQ(got, c.out) << "seed=" << c.seed << " side=" << c.side
                          << " this8=" << c.this8 << " empty=" << c.empty
                          << " mism=" << c.mism;
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed << " side=" << c.side
                           << " this8=" << c.this8 << " empty=" << c.empty
                           << " mism=" << c.mism;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2