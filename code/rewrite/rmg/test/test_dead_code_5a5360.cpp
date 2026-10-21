// 死代码复现 —— 0x5A5360 废弃方案最大函数黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a5360.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a5360_golden.txt（3 种子 × 4 用例 = 12 行）。
// 每例：给定起始点 (x,y)、N 点数、side，函数三连 operator new + 最小堆 +
// 8 方向扩散 + bbox 双分支。对拍：
//   1. 子调用计数 new/del/iterR/iterN/gcell/p486/slot8/slot0/6c10 == golden；
//   2. 调用后下一次 Random() == golden next（随机消耗次数与路径）；
//   3. work 开放区 visited/marked 计数 == golden；
//   4. CELL_OBJ+0x38 终值（复杂路径=哨兵 / 简单路径=0xFFFF）== golden。
//
// ★探针打桩语义（与测试同源）：
//   work 数组为扩展区（菱形边界允许坐标到 ~2side，原版 work 仅 side² 项——
//   越界读堆=UB 死代码从未运行；探针确定化模拟 side² 外 +0x3C=1 挡墙，
//   开放区 = open_rect∩X+Y>side 或全菱形，golden 同源注灌）；
//   GetCellAt → 固定 CELL_OBJ（+0x38 可写 +0x44=-1）；0x486380 → 1；
//   虚表槽 2（0x42F860 扩容）→ 扩容到栈参容量 + 固定 buffer；
//   虚表槽 0（0x42FBF0）→ 记录；0x5A6C10 → 记录；CellIterator → 空。
#include "dead_code_5a5360.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
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
  std::int32_t sx, sy;
  std::int32_t n, side;
  std::int32_t c_new = 0, c_del = 0, c_iter_r = 0, c_iter_n = 0;
  std::int32_t c_gcell = 0, c_p486 = 0, c_slot8 = 0, c_slot0 = 0, c_6c10 = 0;
  std::uint32_t next = 0;
  std::int32_t visited = 0, marked = 0;
  std::uint32_t cell38 = 0;
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a5360_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s;
    int sx, sy, n, side, cn, cd, cir, cin, cg, cp, cs8, cs0, c6, v, m;
    unsigned int nxt, c38;
    // seed=%u start=(%d,%d) N=%d side=%d => new=%d del=%d iterR=%d iterN=%d
    // gcell=%d p486=%d slot8=%d slot0=%d 6c10=%d next=0x%X visited=%d
    // marked=%d cell38=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u start=(%d,%d) N=%d side=%d => new=%d del=%d "
                    "iterR=%d iterN=%d gcell=%d p486=%d slot8=%d slot0=%d "
                    "6c10=%d next=0x%X visited=%d marked=%d cell38=0x%X",
                    &s, &sx, &sy, &n, &side, &cn, &cd, &cir, &cin, &cg, &cp,
                    &cs8, &cs0, &c6, &nxt, &v, &m, &c38) != 18) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.sx = sx;
    c.sy = sy;
    c.n = n;
    c.side = side;
    c.c_new = cn;
    c.c_del = cd;
    c.c_iter_r = cir;
    c.c_iter_n = cin;
    c.c_gcell = cg;
    c.c_p486 = cp;
    c.c_slot8 = cs8;
    c.c_slot0 = cs0;
    c.c_6c10 = c6;
    c.next = nxt;
    c.visited = v;
    c.marked = m;
    c.cell38 = c38;
    cases.push_back(c);
  }
  return cases;
}

// 探针打桩语义的 world（work 扩展区 + 挡墙 + CELL_OBJ 桩 + 方向表 + 记录）
class GoldenWorld : public DeadCode5A5360World {
 public:
  GoldenWorld(std::int32_t side, std::uint32_t abc2b0, bool open_all,
              std::int32_t rx0, std::int32_t ry0, std::int32_t rx1,
              std::int32_t ry1)
      : side_(side), abc2b0_(abc2b0) {
    // work 扩展区：坐标 [0, 4side)。原版菱形边界 A=side、B=3side 允许坐标到
    // ~2side，work 数组仅 side² 项会越界——探针确定化模拟 side² 外 +0x3C=1
    // 挡墙（等价"堆非零"），开放区置 0。golden 与测试同源注灌。
    const int maxc = 4 * side;
    const std::size_t nidx =
        static_cast<std::size_t>(maxc - 1) * side + (maxc - 1) + 1;
    work_.assign(nidx * 0x50, 0xFF);  // 全挡墙（+0x3C 置 1 部分由下方覆盖）
    for (std::size_t i = 0; i < nidx * 0x50; i++) {
      if ((i % 0x50) >= 0x3C && (i % 0x50) < 0x40) {
        work_[i] = 1;  // +0x3C dword 低字节 = 1（挡墙）
      } else {
        work_[i] = 0;
      }
    }
    for (int y = 0; y < maxc; ++y) {
      for (int x = 0; x < maxc; ++x) {
        bool ok;
        if (open_all) {
          ok = (x + y) > side && (x - y) < side && (y - x) < side &&
               (x + y) <= 3 * side;
        } else {
          ok = rx0 <= x && x <= rx1 && ry0 <= y && y <= ry1 &&
               (x + y) > side;
        }
        if (ok) {
          const std::size_t idx = (static_cast<std::size_t>(y) * side + x) * 0x50;
          work_[idx + 0x3C] = 0;  // 开放区 +0x3C = 0
        }
      }
    }
  }

  std::uint32_t WorkVisit(std::uint32_t idx) override {
    return work_[static_cast<std::size_t>(idx) * 0x50 + 0x3C];
  }
  void SetWorkVisit(std::uint32_t idx) override {
    work_[static_cast<std::size_t>(idx) * 0x50 + 0x3C] = 1;
  }
  bool WorkFlag45(std::uint32_t idx) override {
    return work_[static_cast<std::size_t>(idx) * 0x50 + 0x45] != 0;
  }
  void SetWorkFlag45(std::uint32_t idx) override {
    work_[static_cast<std::size_t>(idx) * 0x50 + 0x45] = 1;
  }

  void SetCell38(std::uint32_t /*xy*/, std::uint32_t v) override {
    cell38_ = v;
  }
  std::uint32_t GetCell38(std::uint32_t /*xy*/) override { return cell38_; }
  bool Cell44Neg1(std::uint32_t /*xy*/) override { return true; }  // +0x44=-1

  bool Pred486(std::uint32_t /*xy*/) override { return true; }  // 打桩返 1

  // 方向表（0x89F688 标准 8 方向 N,NE,E,SE,S,SW,W,NW：{dx:int16,dy:int16} 打包）
  std::uint32_t DirOffset(int dir) override {
    static const std::int16_t kDirs[8][2] = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1},
        {0, 1},  {-1, 1}, {-1, 0}, {-1, -1},
    };
    return (static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(kDirs[dir][1])) << 16) |
           (static_cast<std::uint32_t>(
                static_cast<std::uint16_t>(kDirs[dir][0])) & 0xFFFF);
  }

  bool IterNext() override { return false; }  // 空迭代器（跳过清 work）

  bool VecGrow(std::uint32_t /*newcap*/) override { return true; }
  void VecClear() override {}
  void Place(std::int32_t /*t*/) override {}

  std::int32_t side_ = 0;
  std::uint32_t abc2b0_ = 0;
  std::uint32_t cell38_ = 0;
  std::vector<std::uint8_t> work_;

  // 统计开放区（side 坐标内菱形）visited/marked——与探针统计同源
  void Stats(std::int32_t& visited, std::int32_t& marked) {
    visited = 0;
    marked = 0;
    for (int y = 0; y < side_; ++y) {
      for (int x = 0; x < side_; ++x) {
        if (!((x + y) > side_ && (x - y) < side_ && (y - x) < side_ &&
              (x + y) <= 3 * side_)) {
          continue;
        }
        const std::size_t idx =
            (static_cast<std::size_t>(y) * side_ + x) * 0x50;
        if (work_[idx + 0x3C] == 1) visited++;
        if (work_[idx + 0x45] != 0) marked++;
      }
    }
  }
};

TEST(DeadCode5A5360, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    // 用例矩阵与探针同源：
    //   (15,16) N=300 side=24 rect[12,31]² 复杂；N=50 side=16 全开 简单；
    //   N=200 side=8 全开 简单（堆空）；(31,32) N=400 side=24 rect[24,47]² 复杂
    const bool open_all = (c.n == 50 || c.n == 200);
    std::int32_t rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
    if (!open_all) {
      if (c.side == 24 && c.sx == 15) {
        rx0 = 12; ry0 = 12; rx1 = 31; ry1 = 31;
      } else {
        rx0 = 24; ry0 = 24; rx1 = 47; ry1 = 47;
      }
    }
    GoldenWorld world(c.side, 0x12345678, open_all, rx0, ry0, rx1, ry1);
    DeadCode5A5360Context ctx;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.start_x = c.sx;
    ctx.start_y = c.sy;
    ctx.n = c.n;
    ctx.side = c.side;
    ctx.bound_a = c.side;          // [0xABED04] = MapRect.W（方阵=side）
    ctx.bound_b = c.side + 2 * c.side;  // [0xABED08] = W + 2H
    ctx.abc2b0 = 0x12345678;
    DeadCode5A5360(ctx);
    // 1. 子调用计数
    EXPECT_EQ(ctx.calls_new, c.c_new) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_del, c.c_del) << "seed=" << c.seed;
    // iterR（Reset 打桩 1 次）+ iterN（Next 打桩 1 次返回空）= 2 次
    EXPECT_EQ(ctx.calls_iter, c.c_iter_r + c.c_iter_n) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_gcell, c.c_gcell) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_p486, c.c_p486) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_slot8, c.c_slot8) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_slot0, c.c_slot0) << "seed=" << c.seed;
    EXPECT_EQ(ctx.calls_6c10, c.c_6c10) << "seed=" << c.seed;
    // 2. next（随机消耗次数与路径）
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed;
    // 3. work visited/marked
    std::int32_t visited = 0, marked = 0;
    world.Stats(visited, marked);
    EXPECT_EQ(visited, c.visited) << "seed=" << c.seed;
    EXPECT_EQ(marked, c.marked) << "seed=" << c.seed;
    // 4. CELL_OBJ+0x38 终值
    EXPECT_EQ(world.cell38_, c.cell38) << "seed=" << c.seed;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2