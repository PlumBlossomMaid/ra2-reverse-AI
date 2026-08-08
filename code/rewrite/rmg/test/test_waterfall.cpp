// 瀑布 0x59E740 PlaceWaterfall 的黄金对拍
//
// 基准由 code/randmap/dump_waterfall_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/waterfall_golden.txt。函数内部真的会跑 AngularGrow /
// Dilate / CollectBorderCells / PlaceTile / GetCellAt / 正态源，只有
// 0x57A0C0 岸线平滑被钉成恒真恒假（它自己有一份独立基准）。
//
// 每个用例调一次 PlaceWaterfall 之后比六样：
//   1) 返回值与出参 ok —— 两者语义不同，净空扫描否决时是 ret=1 而 ok=0
//   2) 随机数调用次数 —— 两片水面每格各 2 次，AngularGrow 自己一堆
//   3) 游标终值 x / y —— 成功时被推进 12 格
//   4) 共享哑元 0xABDC50 的 +0x24
//   5) 每格终态 (tile, subtile, level, +0x38, +0x3C)
//
// 几条用例是专门盯着容易被"整理干净"的行为造的：
//   wf-blocked / wf-occupied —— 净空扫描否决，**一个随机数都不取**
//   wf-nosmooth              —— 岸线判失败，已涂下去的上游水面**不回滚**
//   wf-split-id              —— 入参 blob 与 +0x308 不同，涂水写前者
//   wf-n-wide / wf-n-narrow / wf-n-dot —— 交替贴砖那个循环的奇偶两条支路
//   wf-dir1 / wf-dir9        —— dir 不在 0/2/4/6：矩形全零；dir/2 == 4 越界读
//   wf-n-edge / wf-small     —— 净空区大半在界外，写入落到共享哑元上
#include "waterfall.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "scaled_random.h"   // FpuChopScope
#include "tile_geometry_fixture.h"
#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

constexpr unsigned kSeed = 0x1234;   // OpsOracle 固定的种子

// 与 dump_place_tile_golden.py 的 THEATER 表逐项对应（温和气候）。
// 贴砖器的两个"地形不可覆盖"谓词会读其中大半，缺一项就对不上。
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
  t.water_cliffs = 148;
  t.ramp_base = 29;
  t.bridge_set = 277;
  t.cliff_set = 39;
  t.cliff_ramps = 601;
  t.ramp_smooth = 621;
  t.water_caves = -1;        // temperate 未定义
  t.wood_bridge_set = -1;
  t.destroyable_cliffs = 761;
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
  int ramp = 0;      // CellClass +0x11C，贴砖器不写，留着看有没有人误写
};

// 区域算子的世界 + 贴砖器的世界，两边同名同签名的那几个写一份实现。
class WaterfallStub : public WaterfallWorld {
 public:
  WaterfallStub(const MapGrid& g, std::vector<PaintWorkCell>* work,
                const TheaterData& t)
      : grid_(g), work_(work), cells_(kCellSlots), exists_(kCellSlots, 0) {
    g.ForEachCell([&](const CellCoord& c) { exists_[SlotOf(c)] = 1; });
    // 四组瀑布砖各 4 块，几何取真实 .tmp 文件头（与 oracle 的 real 剖面同源）
    AddGroup(t.waterfall_north, "wfnorth");
    AddGroup(t.waterfall_east, "wfeast");
    AddGroup(t.waterfall_south, "wfsouth");
    AddGroup(t.waterfall_west, "wfwest");
  }

  // ── RegionWorld ∩ TileWorld ──
  bool CellExists(const CellCoord& c) const override {
    const int s = SlotOf(c);
    return s >= 0 && exists_[s] != 0;
  }
  void SetDummyCoord(const CellCoord& c) override { dummy_coord_ = c; }
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { Mut(c).tile = v; }
  void SetSubTile(const CellCoord& c, int v) override { Mut(c).subtile = v; }
  void SetLevel(const CellCoord& c, int v) override {
    Mut(c).level = static_cast<std::int8_t>(v);
  }

  // ── RegionWorld ──
  int Level(const CellCoord& c) const override { return At(c).level; }

  // ── TileWorld ──
  int SubTile(const CellCoord& c) const override { return At(c).subtile; }
  void SetRamp(const CellCoord& c, int v) override { Mut(c).ramp = v; }
  int DummyIsoTile() const override { return dummy_.tile; }
  int DummySubTile() const override { return dummy_.subtile; }
  void SetDummyTile(int t, int st, int lv) override {
    dummy_.tile = t;
    dummy_.subtile = st;
    dummy_.level = static_cast<std::int8_t>(lv);
  }
  void SetDummyIsoTile(int t) override { dummy_.tile = t; }
  int WorkBlob(const CellCoord& c) const override {
    return (*work_)[WorkIdx(c)].blob_id;
  }
  void SetWorkBlob(const CellCoord& c, int v) override {
    (*work_)[WorkIdx(c)].blob_id = v;
  }

  // ── WaterfallWorld ──
  bool SmoothShoreline(int) override { return smooth_ret_; }
  const TileType* TileByIndex(int index) const override {
    for (const TileType& t : tiles_) {
      if (t.tile_number == index) return &t;
    }
    return nullptr;
  }

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
  void AddGroup(int base, const char* group) {
    for (int k = 0; k < 4; ++k) {
      tiles_.push_back(fixture::MakeTile(
          base + k,
          fixture::RealGeometry().at(std::string(group) + ":" +
                                     std::to_string(k))));
    }
  }
  std::size_t WorkIdx(const CellCoord& c) const {
    // 工作数组按 Y * side + X 索引。基准里的坐标一律在菱形内或紧邻，
    // 落不出 side²；越界就夹住，免得测试自己先崩。
    long long i = static_cast<long long>(c.y) * grid_.side() + c.x;
    if (i < 0) i = 0;
    if (i >= static_cast<long long>(work_->size())) i = work_->size() - 1;
    return static_cast<std::size_t>(i);
  }
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return (s < 0 || !exists_[s]) ? dummy_ : cells_[s];
  }

  MapGrid grid_;
  std::vector<PaintWorkCell>* work_;
  std::vector<Cell> cells_;
  std::vector<char> exists_;
  std::vector<TileType> tiles_;
  Cell dummy_;
  CellCoord dummy_coord_;
  bool smooth_ret_ = true;
};

struct GoldenCase {
  std::string name;
  int w = 0, h = 0;
  int blob = 0, stamp = 0, dir = 0, smooth = 1;
  CellCoord a, b;
  double x0 = 0.0, y0 = 0.0;
  int ret = 0, ok = 0, rng = 0;
  double x = 0.0, y = 0.0;
  CellCoord dummy;
  std::vector<std::array<int, 7>> in;    // x y tile sub lvl owner queued
  std::vector<std::array<int, 7>> out;
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

double NamedDouble(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atof(tok.c_str() + n);
}

bool Has(const std::string& tok, const char* key) {
  return tok.compare(0, std::strlen(key), key) == 0;
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/waterfall_golden.txt";
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
        // terrain= / trect= / orect= / foe= 只是记录输入怎么造的，
        // 具体格子状态一律从 IN 行还原，不参与断言
        if (Has(tok, "terrain=") || Has(tok, "trect=") ||
            Has(tok, "orect=") || Has(tok, "foe=") || tok == "->") {
          continue;
        } else if (Has(tok, "blob=")) {
          c.blob = NamedInt(tok, "blob=");
        } else if (Has(tok, "stamp=")) {
          c.stamp = NamedInt(tok, "stamp=");
        } else if (Has(tok, "a=")) {
          std::sscanf(tok.c_str(), "a=%d,%d", &c.a.x, &c.a.y);
        } else if (Has(tok, "b=")) {
          std::sscanf(tok.c_str(), "b=%d,%d", &c.b.x, &c.b.y);
        } else if (Has(tok, "dir=")) {
          c.dir = NamedInt(tok, "dir=");
        } else if (Has(tok, "smooth=")) {
          c.smooth = NamedInt(tok, "smooth=");
        } else if (Has(tok, "xy0=")) {
          std::sscanf(tok.c_str(), "xy0=%lf,%lf", &c.x0, &c.y0);
        } else if (Has(tok, "ret=")) {
          c.ret = NamedInt(tok, "ret=");
        } else if (Has(tok, "ok=")) {
          c.ok = NamedInt(tok, "ok=");
        } else if (Has(tok, "rng=")) {
          c.rng = NamedInt(tok, "rng=");
        } else if (Has(tok, "x=")) {
          c.x = NamedDouble(tok, "x=");
        } else if (Has(tok, "y=")) {
          c.y = NamedDouble(tok, "y=");
        } else if (Has(tok, "dummy=")) {
          std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      out.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 7> v{};
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

TEST(Waterfall, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  int ok_cases = 0, reject_cases = 0, fail_cases = 0;

  for (const GoldenCase& c : cases) {
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    // 阶段 1 只对菱形内的格子写打包坐标；菱形外永远是 (0,0)。
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    WaterfallStub world(grid, &work, theater);
    world.set_smooth_ret(c.smooth != 0);
    for (const std::array<int, 7>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4]);
      PaintWorkCell& rec = work[static_cast<std::size_t>(grid.WorkIndex(p))];
      rec.blob_id = e[5];
      rec.queued = e[6];
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

    WaterfallParams p;
    p.blob_id = c.blob;
    p.blob_stamp = c.stamp;

    double x = c.x0, y = c.y0;
    bool ok = true;                 // 故意置反：进场那句 `*okOut = 0` 要看得见
    rng.reset_call_count();
    bool ret = false;
    {
      FpuChopScope fpu;   // 全程 x87：PC = 53 位、RC = 向零截断
      ret = PlaceWaterfall(ctx, world, p, c.a, c.b, c.dir, &ok, &x, &y);
    }
    if (c.ok) ++ok_cases;
    if (c.ret && !c.ok) ++reject_cases;
    if (!c.ret) ++fail_cases;

    EXPECT_EQ(ret ? 1 : 0, c.ret) << "case " << c.name;
    EXPECT_EQ(ok ? 1 : 0, c.ok) << "case " << c.name;
    EXPECT_EQ(rng.call_count(), c.rng) << "case " << c.name;
    EXPECT_EQ(x, c.x) << "case " << c.name;
    EXPECT_EQ(y, c.y) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    int bad = 0;
    for (const std::array<int, 7>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      if (got.tile == e[2] && got.subtile == e[3] &&
          static_cast<int>(got.level) == e[4] && rec.blob_id == e[5] &&
          rec.queued == e[6]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << static_cast<int>(got.level)
                      << " owner=" << rec.blob_id << " queued=" << rec.queued
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " owner=" << e[5]
                      << " queued=" << e[6];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";
  }

  // 覆盖面不许悄悄缩水
  EXPECT_GE(cases.size(), 23u);
  EXPECT_GE(ok_cases, 18);
  EXPECT_GE(reject_cases, 2);
  EXPECT_GE(fail_cases, 3);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
