// 湖泊涂块器 0x59C920 的黄金对拍
//
// 基准由 code/randmap/dump_water_blob_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/water_blob_golden.txt。每个用例给一张输入
// （IN 行：tile / subtile / level / +0x38 归属 / +0x3C 戳 / +0x44 放行 / +0x4B），
// 调一次 WaterBlob 之后比五样：
//   1) 返回值
//   2) 随机数调用次数 —— 起点拒绝采样 2/轮、块大小 1/轮、每接受一个邻格 1
//   3) MapSeedClass +0x304（painted_total）的终值
//   4) 每格终态 (tile, subtile, level, +0x38, +0x3C, +0x44, +0x4B)
//   5) 共享哑元 0xABDC50 的 +0x24
//
// ⚠️ 0x57A0C0 岸线平滑在基准侧被钉成恒真/恒假（它自己有一份 17 万字节的
// 独立基准），这里照样用一个返回固定值的桩。
//
// 用例里几条是专门盯着容易被"简化掉"的行为造的：
//   w-wa0            —— WaterAmount == 0 时靠寄存器残值让 budget 变成负数
//   w-solid / w-onlake —— 主循环认领时**不查砖**：起点照涂，四邻全不合格
//   w-wall           —— 收编堆那一遍**要查砖**，与主循环不一致
//   w-mid-dirty44    —— start != (0,0) 时 +0x44 被整块刷成 1
//   w-rnd-dirty44    —— start == (0,0) 时 +0x44 先全清再按 Erode 结果重摆
//   w-rnd-all1 / w-rnd-solid —— 200 次都抽不到合法起点，rng 正好 400
//   w-rnd-nosmooth   —— 涂完了但岸线判失败，整块回滚
#include "water_blob.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "scaled_random.h"   // FpuChopScope
#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

constexpr unsigned kSeed = 0x1234;   // OpsOracle 固定的种子

TheaterData Temperate() {
  TheaterData t;
  t.water_set = 314;
  t.shore_pieces = 89;
  t.green_tile = 493;
  t.clear_to_green_lat = 494;
  t.waterfall_east = 539;
  t.waterfall_west = 547;
  t.waterfall_north = 543;
  t.waterfall_south = 414;
  return t;
}

constexpr int kCellSlots = 0x40000;   // 引擎侧格子数组是 512×512 定长

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int tile = 0;
  int subtile = 0;
  std::int8_t level = 0;
};

// 与 test_region_ops.cpp 同一份最小格子世界，只多一个岸线平滑的桩。
class WaterStub : public WaterBlobWorld {
 public:
  explicit WaterStub(const MapGrid& g)
      : cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
  }

  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }

  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  int Level(const CellCoord& c) const override { return At(c).level; }
  void SetLevel(const CellCoord& c, int v) override {
    Mut(c).level = static_cast<std::int8_t>(v);
  }

  bool SmoothShoreline(int) override { return smooth_ret_; }
  void set_smooth_ret(bool v) { smooth_ret_ = v; }

  Cell Get(const CellCoord& c) const { return At(c); }
  void Seed(const CellCoord& c, int tile, int subtile, int level) {
    Cell& x = Mut(c);
    x.tile = tile;
    x.subtile = subtile;
    x.level = static_cast<std::int8_t>(level);
  }
  CellCoord dummy_coord() const { return dummy_coord_; }

 private:
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }

  std::vector<Cell> cells_;
  std::vector<char> exists_;
  Cell dummy_;
  CellCoord dummy_coord_;
  bool smooth_ret_ = true;
};

struct GoldenCase {
  std::string name;
  int w = 0, h = 0;
  int dirty44 = 0, water = 0, painted = 0, blob = 0, level30c = 0;
  CellCoord start;
  int smooth = 1;
  int ret = 0, rng = 0, total = 0;
  CellCoord dummy;
  std::vector<std::array<int, 9>> in;    // x y tile sub lvl owner queued allow f4b
  std::vector<std::array<int, 9>> out;
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

bool Has(const std::string& tok, const char* key) {
  return tok.compare(0, std::strlen(key), key) == 0;
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/water_blob_golden.txt";
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot open " << path;
  std::vector<GoldenCase> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      GoldenCase c;
      ss >> c.name >> c.w >> c.h;
      std::string tok;
      while (ss >> tok) {
        if (Has(tok, "terrain=") || Has(tok, "owners=") || tok == "->") {
          continue;                      // 只是记录输入怎么造的，不参与断言
        } else if (Has(tok, "dirty44=")) {
          c.dirty44 = NamedInt(tok, "dirty44=");
        } else if (Has(tok, "water=")) {
          c.water = NamedInt(tok, "water=");
        } else if (Has(tok, "painted=")) {
          c.painted = NamedInt(tok, "painted=");
        } else if (Has(tok, "blob=")) {
          c.blob = NamedInt(tok, "blob=");
        } else if (Has(tok, "level30c=")) {
          c.level30c = NamedInt(tok, "level30c=");
        } else if (Has(tok, "start=")) {
          std::sscanf(tok.c_str(), "start=%d,%d", &c.start.x, &c.start.y);
        } else if (Has(tok, "smooth=")) {
          c.smooth = NamedInt(tok, "smooth=");
        } else if (Has(tok, "ret=")) {
          c.ret = NamedInt(tok, "ret=");
        } else if (Has(tok, "rng=")) {
          c.rng = NamedInt(tok, "rng=");
        } else if (Has(tok, "total=")) {
          c.total = NamedInt(tok, "total=");
        } else if (Has(tok, "dummy=")) {
          std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      out.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 9> v{};
      for (int& f : v) ss >> f;
      if (tag == "IN") {
        out.back().in.push_back(v);
      } else {
        out.back().out.push_back(v);
      }
    }
  }
  return out;
}

TEST(WaterBlob, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  int false_cases = 0, rnd_cases = 0;

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    // 阶段 1 只对菱形内的格子写打包坐标；菱形外永远是 (0,0)。
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    WaterStub world(grid);
    world.set_smooth_ret(c.smooth != 0);
    for (const std::array<int, 9>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4]);
      PaintWorkCell& rec = work[static_cast<std::size_t>(grid.WorkIndex(p))];
      rec.blob_id = e[5];
      rec.queued = e[6];
      rec.flag44 = e[7] != 0;
      rec.flag4b = e[8] != 0;
    }

    Randomizer rng(kSeed);
    NormalDeviate normal(rng);
    RegionSet regions;
    RegionContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.theater = &theater;
    ctx.rng = &rng;
    ctx.regions = &regions;
    ctx.normal = &normal;
    ctx.map_w = c.w;
    ctx.map_h = c.h;

    WaterBlobParams p;
    p.water_amount = c.water;
    p.painted_total = c.painted;
    p.blob_id = c.blob;
    p.cell_level = c.level30c;

    rng.reset_call_count();
    bool ret = false;
    {
      FpuChopScope fpu;   // 全程 x87：PC = 53 位、RC = 向零截断
      ret = WaterBlob(ctx, world, p, c.start);
    }
    if (!ret) ++false_cases;
    if (c.start.x == 0 && c.start.y == 0) ++rnd_cases;

    EXPECT_EQ(ret ? 1 : 0, c.ret) << "case " << c.name;
    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(p.painted_total, c.total) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 9>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      if (got.tile == e[2] && got.subtile == e[3] && got.level == e[4] &&
          rec.blob_id == e[5] && rec.queued == e[6] &&
          (rec.flag44 ? 1 : 0) == (e[7] != 0 ? 1 : 0) &&
          (rec.flag4b ? 1 : 0) == (e[8] != 0 ? 1 : 0)) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << static_cast<int>(got.level)
                      << " owner=" << rec.blob_id << " queued=" << rec.queued
                      << " allow=" << rec.flag44 << " f4b=" << rec.flag4b
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " owner=" << e[5]
                      << " queued=" << e[6] << " allow=" << e[7]
                      << " f4b=" << e[8];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  // 覆盖面不许悄悄缩水
  EXPECT_GE(cases.size(), 25u);
  EXPECT_GE(false_cases, 9);
  EXPECT_GE(rnd_cases, 10);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
