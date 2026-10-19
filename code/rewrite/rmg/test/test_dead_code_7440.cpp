// 死代码复现 —— 0x5A7440 菱形域矩形高度一致性校验黄金对拍
//
// 基准由 .qwen/tmp/_golden_7440.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_7440_golden.txt。golden 侧 GetCellAt
// (0x5657A0) 与五个 tile 谓词（0x4866D0/0x4866F0/0x486380/0x486650/
// 0x486670）全部跑真身（hook 只计数不劫持），RNG 不参与（golden 零采样）。
// 下面的 FakeDeadCode7440World 用同一套窗口语义并计数：
//   · CellHeight（0x5657A0 取回 cell 后的 [cell+0x11B] movsx 读）：返回
//     用例高度表值（缺省由 CASE height 供给），记录坐标序并计 gc——一次
//     GetCellAt 计数一次（起点格 + 每个扫描格各一次）
//   · CellTile（同一 cell 的 +0x38 读，不产生新的 GetCellAt）：返回用例
//     tile 表值（缺省 0），不额外计数
//   · IsPaved0（0x4866D0）：tile ∈ [paved, paved+0xf)
//   · IsPaved4（0x4866F0）：tile ∈ [paved4, paved4+4)
//   · IsVoidTile（0x486380）：tile == 0xFFFF || tile == 0
//   · IsMiscPaveTile（0x486650）：tile ∈ [misc, misc+0xE)
//   · IsPaveTile（0x486670）：tile ∈ [pavet, pavet+0x10)
//
// 对拍点八样：返回 al、gc（GetCellAt 次数）、p6d0/p6f0/p380/p650/p670
// 五谓词调用次数 + GetCellAt 坐标序列。10 用例覆盖：
//   all-pass（全 6 格 tile=20 ∈ [15,30) paved + 高度一致 → 6D0 全过 +
//   flagA=1 全过, gc=7=起点1+扫描6）、corner0-fail（x=3,y=6 →
//   X+Y=9<=W=12 首判据 fail, gc=0）、flagA-zero（全 paved 但 flagA=0
//   → 首格 6D0 命中后整体 fail, gc=2=起点1+首格1, p6d0=1）、
//   p380-pass（0xFFFF 直达 380）、p650-pass（205 ∈ [200,214)）、
//   p670-pass（30 ∈ [16,32) 且避开 [15,30) paved → 直达 670）、
//   height-mismatch（末格 (10,7) 高度 9≠7 → 高度比对在谓词前 fail,
//   gc=7 p6d0=5 —— 末格未进谓词链）、h-zero（h=0 → 矩形空但四角与
//   起点格仍判, ret=1 gc=1）、w-zero（w=0 → 内层空, ret=1 gc=1）、
//   all-fail（500 五谓词全拒 → 首格即 fail, gc=2 p6d0=1 p6f0=1
//   p380=1 p650=1 p670=1）。
//
// ★差异提醒（vs batch 934 的 0x5A7250）：7440 只有 dl 单标志（6D0/6F0
// 共用 flagA，无栈参 flagB），且多起点格期望高度 + 每格高度一致性比对
// （高度不等直接 fail、不查谓词）。
//
// 菱形域取自 golden 环境：W=12、H=8（dump_tech_buildings_golden 的
// W,H），[0xABED04]=12、[0xABED08]=12+2*8=28（PaintOracle 基类灌值）。
#include "dead_code_7440.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct Case {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;
  int flag_a = 0;
  int paved = 0, paved4 = 0, pavet = 0, misc = 0;
  int height = 0;   // CASE 全局高度（未单列 IN 的格用它）
  int ret = 0, gc = 0, p6d0 = 0, p6f0 = 0, p380 = 0, p650 = 0, p670 = 0;
  // IN 行：x y tile height
  std::map<std::pair<int, int>, std::pair<std::int32_t, std::int8_t> > cells;
  // GC 行：GetCellAt 顺序坐标
  std::vector<std::pair<int, int> > gc_seq;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_7440_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "CASE") {
      Case c;
      std::string arrow;
      is >> c.name >> c.x >> c.y >> c.w >> c.h
         >> c.flag_a
         >> c.paved >> c.paved4 >> c.pavet >> c.misc
         >> c.height
         >> arrow >> c.ret >> c.gc >> c.p6d0 >> c.p6f0
         >> c.p380 >> c.p650 >> c.p670;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "IN") {
      int ix = 0, iy = 0, tile = 0, hv = 0;
      is >> ix >> iy >> tile >> hv;
      c.cells[std::make_pair(ix, iy)] =
          std::make_pair(static_cast<std::int32_t>(tile),
                         static_cast<std::int8_t>(hv));
    } else if (key == "GC") {
      int gx = 0, gy = 0;
      is >> gx >> gy;
      c.gc_seq.push_back(std::make_pair(gx, gy));
    }
  }
  return cases;
}

class FakeDeadCode7440World : public DeadCode7440World {
 public:
  explicit FakeDeadCode7440World(const Case& c) : c_(c) {}

  std::int8_t CellHeight(int x, int y) override {
    ++gc_calls_;   // 一次 GetCellAt = 一次 gc
    gc_seq_.push_back(std::make_pair(x, y));
    const std::map<std::pair<int, int>,
                   std::pair<std::int32_t, std::int8_t> >::const_iterator it =
        c_.cells.find(std::make_pair(x, y));
    return it == c_.cells.end()
               ? static_cast<std::int8_t>(c_.height)
               : it->second.second;
  }
  std::int32_t CellTile(int x, int y) override {
    // 同一 cell 内的 +0x38 读：不再产生新的 GetCellAt（不计数）
    const std::map<std::pair<int, int>,
                   std::pair<std::int32_t, std::int8_t> >::const_iterator it =
        c_.cells.find(std::make_pair(x, y));
    return it == c_.cells.end() ? 0 : it->second.first;
  }
  bool IsPaved0(std::int32_t tile) override {
    ++p6d0_;
    return tile >= c_.paved && tile < c_.paved + 0xf;
  }
  bool IsPaved4(std::int32_t tile) override {
    ++p6f0_;
    return tile >= c_.paved4 && tile < c_.paved4 + 4;
  }
  bool IsVoidTile(std::int32_t tile) override {
    ++p380_;
    return tile == 0xFFFF || tile == 0;
  }
  bool IsMiscPaveTile(std::int32_t tile) override {
    ++p650_;
    return tile >= c_.misc && tile < c_.misc + 0xE;
  }
  bool IsPaveTile(std::int32_t tile) override {
    ++p670_;
    return tile >= c_.pavet && tile < c_.pavet + 0x10;
  }

  int gc_calls_ = 0, p6d0_ = 0, p6f0_ = 0, p380_ = 0, p650_ = 0, p670_ = 0;
  std::vector<std::pair<int, int> > gc_seq_;

 private:
  const Case& c_;
};

TEST(DeadCode7440, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(10u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    FakeDeadCode7440World world(c);
    DeadCode7440Context ctx;
    ctx.world = &world;
    ctx.raw_x = static_cast<std::uint32_t>(c.x);
    ctx.raw_y = static_cast<std::uint32_t>(c.y);
    ctx.w = c.w;
    ctx.h = c.h;
    ctx.flag_a = static_cast<std::uint8_t>(c.flag_a);
    ctx.w_bound = 12;            // [0xABED04]（golden 环境 W=12）
    ctx.w2h_bound = 12 + 2 * 8;  // [0xABED08] = W+2H = 28
    ctx.paved = c.paved;
    ctx.paved4 = c.paved4;
    ctx.pavet = c.pavet;
    ctx.misc = c.misc;

    const int got = DeadCode7440(ctx);

    EXPECT_EQ(c.ret, got) << "返回 al 不对";
    EXPECT_EQ(c.gc, world.gc_calls_) << "GetCellAt 调用次数不对";
    EXPECT_EQ(c.p6d0, world.p6d0_) << "0x4866D0 调用次数不对";
    EXPECT_EQ(c.p6f0, world.p6f0_) << "0x4866F0 调用次数不对";
    EXPECT_EQ(c.p380, world.p380_) << "0x486380 调用次数不对";
    EXPECT_EQ(c.p650, world.p650_) << "0x486650 调用次数不对";
    EXPECT_EQ(c.p670, world.p670_) << "0x486670 调用次数不对";
    ASSERT_EQ(c.gc_seq.size(), world.gc_seq_.size())
        << "GetCellAt 坐标序列长度不对";
    for (std::size_t i = 0; i < c.gc_seq.size(); ++i) {
      EXPECT_EQ(c.gc_seq[i].first, world.gc_seq_[i].first)
          << "第 " << i << " 次 GetCellAt 的 X 不对";
      EXPECT_EQ(c.gc_seq[i].second, world.gc_seq_[i].second)
          << "第 " << i << " 次 GetCellAt 的 Y 不对";
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2