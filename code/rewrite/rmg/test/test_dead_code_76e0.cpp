// 死代码复现 —— 0x5A76E0 菱形域矩形完整落点谓词黄金对拍
//
// 基准由 .qwen/tmp/_golden_76E0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_76e0_golden.txt。golden 侧 GetCellAt
// (0x5657A0) 与四个 tile 谓词（0x486670/0x486380/0x486650/0x486730）
// 全部跑真身（hook 只计数不劫持），RNG 不参与（golden 零采样）。
// 下面的 FakeDeadCode76E0World 用同一套窗口语义并计数：
//   · CellTile（0x5657A0）：返回用例 tile 表值（缺省 0），记录坐标序
//   · IsPaveTile（0x486670）：tile ∈ [pavet, pavet+16)
//   · IsVoidTile（0x486380）：tile == 0xFFFF || tile == 0
//   · IsMiscPaveTile（0x486650）：tile ∈ [misc, misc+14)
//   · IsMedianTile（0x486730）：tile ∈ [medians, medians+14)
//
// 对拍点六样：返回 al、gc（GetCellAt 次数）、p670/p380/p650/p730 四谓词
// 调用次数 + GetCellAt 坐标序列。10 用例覆盖：
//   all-pass（全过）、corner0/1/2-fail（四角判据各自失败路径）、
//   inner-fail（内块第 5 格 (9,7)=500 不在 medians 窗口 → 0x486730 拒）、
//   inner-medians（(9,7)=105 ∈ [100,114) 通过）、
//   inner-paved+1（(9,7)=16 ∈ 闭区间 [15,16] 直通、不调 0x486730——
//   ★paved 快路径是闭区间而三个窗口谓词是半开区间，golden p730=0 实锤）、
//   edge-ffff（边带 0xFFFF → 0x486380 过）、edge-misc（边带 205 ∈
//   [200,214) → 0x486650 过）、edge-fail（边带 300 三谓词全拒 → 第 1 格
//   即 fail，gc=10 实锤边带短路 + x 优先遍历序）。
//
// 菱形域取自 golden 环境：W=12、H=8（dump_tech_buildings_golden 的
// W,H），[0xABED04]=12、[0xABED08]=12+2*8=28（PaintOracle 基类灌值）。
#include "dead_code_76e0.h"

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
  int paved = 0, medians = 0, pavet = 0, misc = 0;
  int ret = 0, gc = 0, p670 = 0, p380 = 0, p650 = 0, p730 = 0;
  // IN 行：tile 表（含缺省 tile=0 之外的显式设定格）
  std::map<std::pair<int, int>, std::int32_t> tiles;
  // GC 行：GetCellAt 顺序坐标
  std::vector<std::pair<int, int> > gc_seq;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_76e0_golden.txt";
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
         >> c.paved >> c.medians >> c.pavet >> c.misc
         >> arrow >> c.ret >> c.gc >> c.p670 >> c.p380 >> c.p650 >> c.p730;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "IN") {
      int ix = 0, iy = 0;
      std::uint32_t tile = 0;
      is >> ix >> iy >> tile;
      c.tiles[std::make_pair(ix, iy)] = static_cast<std::int32_t>(tile);
    } else if (key == "GC") {
      int gx = 0, gy = 0;
      is >> gx >> gy;
      c.gc_seq.push_back(std::make_pair(gx, gy));
    }
  }
  return cases;
}

class FakeDeadCode76E0World : public DeadCode76E0World {
 public:
  explicit FakeDeadCode76E0World(const Case& c) : c_(c) {}

  std::int32_t CellTile(int x, int y) override {
    ++gc_calls_;
    gc_seq_.push_back(std::make_pair(x, y));
    const std::map<std::pair<int, int>, std::int32_t>::const_iterator it =
        c_.tiles.find(std::make_pair(x, y));
    return it == c_.tiles.end() ? 0 : it->second;
  }
  bool IsPaveTile(std::int32_t tile) override {
    ++p670_;
    return tile >= c_.pavet && tile < c_.pavet + 16;
  }
  bool IsVoidTile(std::int32_t tile) override {
    ++p380_;
    return tile == 0xFFFF || tile == 0;
  }
  bool IsMiscPaveTile(std::int32_t tile) override {
    ++p650_;
    return tile >= c_.misc && tile < c_.misc + 14;
  }
  bool IsMedianTile(std::int32_t tile) override {
    ++p730_;
    return tile >= c_.medians && tile < c_.medians + 14;
  }

  int gc_calls_ = 0, p670_ = 0, p380_ = 0, p650_ = 0, p730_ = 0;
  std::vector<std::pair<int, int> > gc_seq_;

 private:
  const Case& c_;
};

TEST(DeadCode76E0, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(10u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    FakeDeadCode76E0World world(c);
    DeadCode76E0Context ctx;
    ctx.world = &world;
    ctx.raw_x = static_cast<std::uint32_t>(c.x);
    ctx.raw_y = static_cast<std::uint32_t>(c.y);
    ctx.w = c.w;
    ctx.h = c.h;
    ctx.w_bound = 12;        // [0xABED04]（golden 环境 W=12）
    ctx.w2h_bound = 12 + 2 * 8;  // [0xABED08] = W+2H = 28
    ctx.paved = c.paved;
    ctx.medians = c.medians;
    ctx.pavet = c.pavet;
    ctx.misc = c.misc;

    const int got = DeadCode76E0(ctx);

    EXPECT_EQ(c.ret, got) << "返回 al 不对";
    EXPECT_EQ(c.gc, world.gc_calls_) << "GetCellAt 调用次数不对";
    EXPECT_EQ(c.p670, world.p670_) << "0x486670 调用次数不对";
    EXPECT_EQ(c.p380, world.p380_) << "0x486380 调用次数不对";
    EXPECT_EQ(c.p650, world.p650_) << "0x486650 调用次数不对";
    EXPECT_EQ(c.p730, world.p730_) << "0x486730 调用次数不对";
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