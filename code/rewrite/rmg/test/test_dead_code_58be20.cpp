// 死代码复现 —— 0x58BE20 随机取「已占用」格子黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_58be20.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_58be20_golden.txt（5 种子 × 8 用例 = 40 行）。
// 每例：给定 seed/side/empty，函数从 work 数组拒绝采样取「已占用」格。
// 对拍的四个东西：
//   1. 返回 dword（work[v] 首 dword = x|(y<<16)）逐位一致（BitEq/Eq）；
//   2. 下一次 Random() 的值 == golden next（校验随机消耗次数一致——
//      拒绝采样路径上每一步都吃一个随机数，任何一次多抽/少抽都会错位）。
#include "dead_code_58be20.h"

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
  int empty;        // work 前 empty 项为 (0,0) 空格子
  std::uint32_t out;   // 期望返回 dword
  std::uint32_t next;  // 调用后下一次 Random() 值
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_58be20_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, o, n;
    int side, emp;
    // seed=%u side=%d empty=%d => out=0x%X next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u side=%d empty=%d => out=0x%X next=0x%X",
                    &s, &side, &emp, &o, &n) != 5) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.side = side;
    c.empty = emp;
    c.out = o;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// work 构造与探针 make_work（.qwen/tmp/_emu_deadcode_58be20.py）一致：
// 前 empty 项 = (0,0)，其余第 i 项 = (i, i+1)（即 dword = i | ((i+1)<<16)）。
std::vector<std::uint32_t> MakeWork(int side, int empty) {
  const int n = side * side;
  std::vector<std::uint32_t> w;
  w.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    if (i < empty) {
      w.push_back(0);
    } else {
      w.push_back(static_cast<std::uint32_t>(i) |
                  (static_cast<std::uint32_t>(i + 1) << 16));
    }
  }
  return w;
}

TEST(DeadCode58BE20, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    DeadCode58BE20Context ctx;
    ctx.rng = &rng;
    ctx.side = c.side;
    ctx.work = MakeWork(c.side, c.empty);
    const std::uint32_t got = DeadCode58BE20(ctx);
    EXPECT_EQ(got, c.out) << "seed=" << c.seed << " side=" << c.side
                          << " empty=" << c.empty;
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed << " side=" << c.side
                           << " empty=" << c.empty;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2