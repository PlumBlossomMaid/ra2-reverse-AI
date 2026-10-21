// 死代码复现 —— 0x58C5A0 随机取「已占用且类型匹配且双谓词通过」格子黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_58c5a0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_58c5a0_golden.txt（5 种子 × 8 用例 = 40 行）。
// 每例：给定 seed/side/f38/arg2/arg3/tile/d56/empty/mism，函数从 work 数组
// 拒绝采样取「非空格且 0x56D230(cell) 返回值==arg2 且 GetCellAt 谓词通过且
// （arg3==-1 或 [0x38]==arg3）」的格。
// 对拍：
//   1. 返回 dword（成功 work[v] 首 dword / 100 次上限后 0）；
//   2. 下一次 Random() 的值 == golden next（校验随机消耗次数一致——
//      拒绝采样、各重抽路径、100 次上限的消耗都要逐位对上）。
#include "dead_code_58c5a0.h"

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

struct Case {
  std::uint32_t seed;
  int side;
  std::uint32_t f38;     // work 记录 [+0x38] 类型字段
  std::uint32_t arg2;    // 0x56D230 返回值比较基准
  int arg3;              // 栈参1：==-1 跳过 [0x38] 比对
  int tile;              // GetCellAt 返回 cell 的 +0x38（0 → 0x486380 通过）
  std::uint32_t d56;     // 0x56D230 打桩返回值（须 == arg2 才通过）
  int empty;             // work 前 empty 项为 (0,0) 空格子
  int mism;              // 之后 mism 项 [+0x38]=f38^0x55（类型不匹配）
  std::uint32_t out;     // 期望返回 dword
  std::uint32_t next;    // 调用后下一次 Random() 值
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_58c5a0_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, f38, a2, d56, o, n;
    int side, a3, tile, emp, mism;
    // seed=%u side=%d f38=%u arg2=%u arg3=%d tile=%d d56=%u empty=%d mism=%d => out=0x%X next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u side=%d f38=%u arg2=%u arg3=%d tile=%d d56=%u empty=%d mism=%d => out=0x%X next=0x%X",
                    &s, &side, &f38, &a2, &a3, &tile, &d56, &emp, &mism, &o,
                    &n) != 11) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.side = side;
    c.f38 = f38;
    c.arg2 = a2;
    c.arg3 = a3;
    c.tile = tile;
    c.d56 = d56;
    c.empty = emp;
    c.mism = mism;
    c.out = o;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// work 构造与探针 make_work（.qwen/tmp/_emu_deadcode_58c5a0.py）一致：
// 前 empty 项为 (0,0) 空格子；之后 mism 项坐标 (i,i+1) 但 [+0x38]=f38^0x55；
// 其余坐标 (i,i+1) 且 [+0x38]=f38。
std::vector<DeadCode58C5A0Rec> MakeWork(int side, std::uint32_t f38,
                                        int empty, int mism) {
  const int n = side * side;
  std::vector<DeadCode58C5A0Rec> w;
  w.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    DeadCode58C5A0Rec r;
    if (i < empty) {
      r.coord = 0;             // (0,0) 空格子
      r.field38 = f38;
    } else if (i < empty + mism) {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = f38 ^ 0x55u;  // 类型不匹配（0x56D230 之外的第二道判定）
    } else {
      r.coord = static_cast<std::uint32_t>(i) |
                (static_cast<std::uint32_t>(i + 1) << 16);
      r.field38 = f38;          // 类型匹配
    }
    w.push_back(r);
  }
  return w;
}

// 0x56D230 打桩：golden 探针在入口写 mov eax,imm; ret 0xC 注入固定 d56。
// 0x486380 谓词：cell+0x38 == 0xFFFF || == 0 → 通过（真）。
class GoldenWorld : public DeadCode58C5A0World {
 public:
  GoldenWorld(std::uint32_t d56, int tile)
      : d56_(d56), tile_(tile) {}
  Randomizer& Rng() override { return *rng_; }
  std::uint32_t D56Value(int x, int y) override {
    (void)x;
    (void)y;
    return d56_;  // 打桩固定返回值
  }
  bool CellPass(int x, int y) override {
    (void)x;
    (void)y;
    return tile_ == 0xFFFF || tile_ == 0;  // 0x486380 真身语义
  }
  Randomizer* rng_ = nullptr;
  std::uint32_t d56_;
  int tile_;
};

TEST(DeadCode58C5A0, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    GoldenWorld world(c.d56, c.tile);
    world.rng_ = &rng;
    DeadCode58C5A0Context ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.side = c.side;
    ctx.arg2 = c.arg2;
    ctx.arg3 = c.arg3;
    ctx.work = MakeWork(c.side, c.f38, c.empty, c.mism);
    const std::uint32_t got = DeadCode58C5A0(ctx);
    EXPECT_EQ(got, c.out) << "seed=" << c.seed << " side=" << c.side
                          << " f38=" << c.f38 << " arg2=" << c.arg2
                          << " arg3=" << c.arg3 << " tile=" << c.tile
                          << " d56=" << c.d56 << " empty=" << c.empty
                          << " mism=" << c.mism;
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed
                           << " side=" << c.side << " f38=" << c.f38
                           << " arg2=" << c.arg2 << " arg3=" << c.arg3
                           << " tile=" << c.tile << " d56=" << c.d56
                           << " empty=" << c.empty << " mism=" << c.mism;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2