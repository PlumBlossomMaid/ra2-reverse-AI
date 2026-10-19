// 死代码复现 —— 0x5A7250 菱形域矩形扫描校验黄金对拍
//
// 基准由 .qwen/tmp/_golden_7250.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_7250_golden.txt。golden 侧 GetCellAt
// (0x5657A0) 与五个 tile 谓词（0x4866D0/0x4866F0/0x486380/0x486650/
// 0x486670）全部跑真身（hook 只计数不劫持），RNG 不参与（golden 零采样）。
// 下面的 FakeDeadCode7250World 用同一套窗口语义并计数：
//   · CellTile（0x5657A0）：返回用例 tile 表值（缺省 0），记录坐标序
//   · IsPaved0（0x4866D0）：tile ∈ [paved, paved+0x10)
//   · IsPaved4（0x4866F0）：tile ∈ [paved4, paved4+4)
//   · IsVoidTile（0x486380）：tile == 0xFFFF || tile == 0
//   · IsMiscPaveTile（0x486650）：tile ∈ [misc, misc+0xE)
//   · IsPaveTile（0x486670）：tile ∈ [pavet, pavet+0x10)
//
// 对拍点八样：返回 al、gc（GetCellAt 次数）、p6d0/p6f0/p380/p650/p670
// 五谓词调用次数 + GetCellAt 坐标序列。14 用例覆盖：
//   all-pass（全 6 格 paved → 6D0 全过 + flagA=1）、
//   corner0-fail（x=3,y=6 → X+Y=9<=W=12 首判据 fail, gc=0）、
//   corner1-fail（w=20 → 角1 X+Y=33>W+2H=28 fail, gc=0）、
//   flagA-zero（全 paved 但 flagA=0 → 首格整体 fail, gc=1 —— 双前置
//   谓词的 flag 门控是整体失败不是跳过）、
//   flagB-zero（全 tile=12 ∈ [11,15) → 6F0 过 + flagB=0 → gc=1,
//   p6d0=1 p6f0=1 实锤 6D0 也先被调一次未命中）、
//   flagB-ok（flagB=1 → 全 6 格过, gc=6）、
//   p380-pass（0xFFFF 直达 380）、p650-pass（205 ∈ [200,214)）、
//   p670-pass（30 ∈ [16,32) 且避开 [15,25) paved → 直达 670）、
//   all-fail（500 五谓词全拒 → 首格即 fail, gc=1 短路）、
//   h-zero（h=0 → 四角仍判、扫描层 cy<h+y 恒假 → ret=1 gc=0）、
//   w-zero（w=0 → 内层空、外层 cy 走两轮但从不 GetCellAt → ret=1
//   gc=0——四角仍判）、mixed-firstfail（首格 500 全拒 → gc=1）、
//   mixed-last（末格 12 走 6F0/flagB 分支 → 全 6 格过 gc=6 p6f0=1）。
//
// 菱形域取自 golden 环境：W=12、H=8（dump_tech_buildings_golden 的
// W,H），[0xABED04]=12、[0xABED08]=12+2*8=28（PaintOracle 基类灌值）。
#include "dead_code_7250.h"

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
  int flag_a = 0, flag_b = 0;
  int paved = 0, paved4 = 0, pavet = 0, misc = 0;
  int ret = 0, gc = 0, p6d0 = 0, p6f0 = 0, p380 = 0, p650 = 0, p670 = 0;
  // IN 行：tile 表（含缺省 tile=0 之外的显式设定格）
  std::map<std::pair<int, int>, std::int32_t> tiles;
  // GC 行：GetCellAt 顺序坐标
  std::vector<std::pair<int, int> > gc_seq;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_7250_golden.txt";
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
         >> c.flag_a >> c.flag_b
         >> c.paved >> c.paved4 >> c.pavet >> c.misc
         >> arrow >> c.ret >> c.gc >> c.p6d0 >> c.p6f0
         >> c.p380 >> c.p650 >> c.p670;
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

class FakeDeadCode7250World : public DeadCode7250World {
 public:
  explicit FakeDeadCode7250World(const Case& c) : c_(c) {}

  std::int32_t CellTile(int x, int y) override {
    ++gc_calls_;
    gc_seq_.push_back(std::make_pair(x, y));
    const std::map<std::pair<int, int>, std::int32_t>::const_iterator it =
        c_.tiles.find(std::make_pair(x, y));
    return it == c_.tiles.end() ? 0 : it->second;
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

TEST(DeadCode7250, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(14u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    FakeDeadCode7250World world(c);
    DeadCode7250Context ctx;
    ctx.world = &world;
    ctx.raw_x = static_cast<std::uint32_t>(c.x);
    ctx.raw_y = static_cast<std::uint32_t>(c.y);
    ctx.w = c.w;
    ctx.h = c.h;
    ctx.flag_a = static_cast<std::uint8_t>(c.flag_a);
    ctx.flag_b = static_cast<std::uint8_t>(c.flag_b);
    ctx.w_bound = 12;        // [0xABED04]（golden 环境 W=12）
    ctx.w2h_bound = 12 + 2 * 8;  // [0xABED08] = W+2H = 28
    ctx.paved = c.paved;
    ctx.paved4 = c.paved4;
    ctx.pavet = c.pavet;
    ctx.misc = c.misc;

    const int got = DeadCode7250(ctx);

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