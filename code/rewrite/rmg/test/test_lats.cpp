// 阶段 10「Creating LATs, rocks etc」的黄金对拍
//
// 基准由 code/randmap/dump_lats_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/lats_golden.txt。四类入口：
//   write —— 0x5A4B60 LAT 块写入器（含 allowNonPlain 那条死分支）
//   tree  —— 0x5A45E0 树林补丁生长（返回值 = 真正栽下的树数）
//   veg   —— 0x5A38C0 播概率图（含「5×5 邻域写的全是中心格」那个原版 bug）
//   temp / snow —— 0x59921F 那段调度连着两条主体，整段跑
//
// 比七样：取数个数、返回值、SetupLAT 次数、TREExx 名字与落点、
// RecalcAttributes 的 (格子, 参数)、以及每一格跑完之后的
// (砖号, overlay, +0x11E, 工作记录 +0x3C/+0x47, 三个概率槽)。
#include "lats.h"

#include <cstdlib>
#include <fstream>
#include <map>
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

// 一格的全部可观测字段。缺省值 = 基准脚本里的 DEFAULT。
struct CellState {
  int tile = 0;
  int ramp = 0;
  int overlay = -1;
  int e4 = 0;
  int ec = 0;
  int odata = 0;   // +0x11E，只在结果里出现
  int queued = 0;
  int flag45 = 0;
  int flag47 = 0;
  double rough = 0.0;
  double green = 0.0;
  double sand = 0.0;
};

struct Terr {
  std::string name;
  int x = 0, y = 0;
};
struct Recalc {
  int x = 0, y = 0, arg = 0;
};

struct Case {
  std::string name, kind;
  int w = 0, h = 0, veg = 0, width = 0, max_trees = 0;
  unsigned rng_seed = 0;
  // write: sx sy tile count field allow；tree: sx sy size prob
  int a0 = 0, a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0;
  double prob = 0.0;
  int rng = 0, ret = -1, setuplat = 0, nterr = 0, nrecalc = 0;
  std::map<long long, CellState> in;
  std::map<long long, CellState> out;
  std::vector<Terr> terr;
  std::vector<Recalc> recalc;
};

long long Key(int x, int y) { return (static_cast<long long>(y) << 9) + x; }

std::vector<Case> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/lats_golden.txt";
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
      is >> c.name >> c.kind >> c.w >> c.h >> c.rng_seed >> c.veg >> c.width
         >> c.max_trees;
      if (c.kind == "write") {
        is >> c.a0 >> c.a1 >> c.a2 >> c.a3 >> c.a4 >> c.a5;
      } else if (c.kind == "tree") {
        is >> c.a0 >> c.a1 >> c.a2 >> c.prob;
      }
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "IN") {
      int x = 0, y = 0;
      CellState s;
      is >> x >> y >> s.tile >> s.ramp >> s.overlay >> s.e4 >> s.ec >> s.queued
         >> s.flag45 >> s.flag47 >> s.rough >> s.green >> s.sand;
      c.in[Key(x, y)] = s;
    } else if (key == "RES") {
      is >> c.rng >> c.ret >> c.setuplat >> c.nterr >> c.nrecalc;
    } else if (key == "OUT") {
      int x = 0, y = 0;
      CellState s;
      is >> x >> y >> s.tile >> s.overlay >> s.odata >> s.queued >> s.flag47
         >> s.rough >> s.green >> s.sand;
      c.out[Key(x, y)] = s;
    } else if (key == "TERR") {
      Terr t;
      is >> t.name >> t.x >> t.y;
      c.terr.push_back(t);
    } else if (key == "RECALC") {
      Recalc r;
      is >> r.x >> r.y >> r.arg;
      c.recalc.push_back(r);
    }
  }
  return cases;
}

// 与基准脚本的五个桩一一对应的世界。
//
// ⚠️ 菱形外的坐标在原版里全部落到同一份哑元 0xABDC50 上：读到的是上一次的
// 残留，写进去也不影响任何真实格子。这里用一条独立的 dummy_ 记录复现。
class FakeLatWorld : public LatWorld {
 public:
  struct Cell {
    int tile = 0;
    int ramp = 0;
    int overlay = -1;
    int odata = 0;
    int e4 = 0;
    int ec = 0;
  };

  FakeLatWorld(const MapGrid& grid, const TheaterData& th)
      : grid_(grid), th_(th) {}

  Cell& At(const CellCoord& c) {
    if (!grid_.InMap(c)) return dummy_;
    return cells_[Key(c.x, c.y)];
  }

  int IsoTile(const CellCoord& c) override { return At(c).tile; }
  void SetIsoTile(const CellCoord& c, int v) override { At(c).tile = v; }
  int Ramp(const CellCoord& c) override { return At(c).ramp; }
  int Overlay(const CellCoord& c) override { return At(c).overlay; }
  void SetOverlay(const CellCoord& c, int v) override { At(c).overlay = v; }
  void SetOverlayData(const CellCoord& c, int v) override {
    At(c).odata = v & 0xFF;
  }
  int Field0xE4(const CellCoord& c) override { return At(c).e4; }
  int Field0xEC(const CellCoord& c) override { return At(c).ec; }

  // 0x486670：砖号 ∈ [PaveTile, +0x10)
  bool IsPaveTile(const CellCoord& c) override {
    const int t = At(c).tile;
    return t >= th_.pave_tile && t < th_.pave_tile + 0x10;
  }
  // 0x486650：砖号 ∈ [MiscPaveTile, +0x0E)
  bool IsMiscPaveTile(const CellCoord& c) override {
    const int t = At(c).tile;
    return t >= th_.misc_pave_tile && t < th_.misc_pave_tile + 0x0E;
  }
  // 0x4865B0：砖号 ∈ [ShorePieces, +0x2A)
  bool IsShorePiece(const CellCoord& c) override {
    const int t = At(c).tile;
    return t >= th_.shore_pieces && t < th_.shore_pieces + 0x2A;
  }
  // 0x486790：砖号 ∈ [ClearToSandLat, +0x10)
  bool IsClearToSandLat(const CellCoord& c) override {
    const int t = At(c).tile;
    return t >= th_.clear_to_sand_lat && t < th_.clear_to_sand_lat + 0x10;
  }
  // 0x4867B0：== GreenTile，或 ∈ [ClearToGreenLat, +0x10)
  bool IsGreen(const CellCoord& c) override {
    const int t = At(c).tile;
    if (t == th_.green_tile) return true;
    return t >= th_.clear_to_green_lat && t < th_.clear_to_green_lat + 0x10;
  }

  void SetupLAT(const CellCoord& c) override { (void)c; ++setuplat; }
  void RecalcAttributes(const CellCoord& c, int arg) override {
    Recalc r;
    r.x = c.x;
    r.y = c.y;
    r.arg = arg;
    recalcs.push_back(r);
  }
  void CreateTerrain(const char* name, const CellCoord& at) override {
    Terr t;
    t.name = name;
    t.x = at.x;
    t.y = at.y;
    terrains.push_back(t);
  }

  int setuplat = 0;
  std::vector<Terr> terrains;
  std::vector<Recalc> recalcs;

 private:
  MapGrid grid_;
  TheaterData th_;
  Cell dummy_;
  std::map<long long, Cell> cells_;
};

// temperate 的砖号全局，取自 memory/data/rmg/tileset_starts.txt。
// pave 两项温和气候里其实没有，基准脚本给了合成值好把死分支走一遍。
TheaterData MakeTheater() {
  TheaterData th;
  th.shore_pieces = 89;
  th.green_tile = 493;
  th.clear_to_green_lat = 494;
  th.clear_tile = 0;
  th.rough_tile = 131;
  th.sand_tile = 418;
  th.clear_to_sand_lat = 419;
  th.black_tile = 1;
  th.pave_tile = 600;
  th.misc_pave_tile = 700;
  return th;
}

TEST(Lats, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(25u, cases.size());
  const TheaterData th = MakeTheater();

  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    MapGrid grid(c.w, c.h);
    const int side = grid.side();
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    // 阶段 1 只给菱形内的格子填打包坐标；菱形外的槽位恒为 (0,0)
    grid.ForEachCell([&](const CellCoord& cc) {
      work[static_cast<std::size_t>(cc.y * side + cc.x)].coord = cc;
    });
    FakeLatWorld world(grid, th);
    for (std::map<long long, CellState>::const_iterator it = c.in.begin();
         it != c.in.end(); ++it) {
      const CellState& s = it->second;
      const CellCoord cc{static_cast<int>(it->first & 0x1FF),
                         static_cast<int>(it->first >> 9)};
      FakeLatWorld::Cell& cell = world.At(cc);
      cell.tile = s.tile;
      cell.ramp = s.ramp;
      cell.overlay = s.overlay;
      cell.e4 = s.e4;
      cell.ec = s.ec;
      PaintWorkCell& w = work[static_cast<std::size_t>(cc.y * side + cc.x)];
      w.queued = s.queued;
      w.flag45 = s.flag45 != 0;
      w.flag47 = s.flag47 != 0;
      w.prob_rough = s.rough;
      w.prob_green = s.green;
      w.prob_sand = s.sand;
    }

    Randomizer rng(c.rng_seed);
    NormalDeviate normal(rng);
    LatContext ctx;
    ctx.grid = grid;
    ctx.work = &work;
    ctx.world = &world;
    ctx.rng = &rng;
    ctx.normal = &normal;
    ctx.side = side;
    ctx.theater = &th;
    ctx.params.vegetation = c.veg;
    ctx.params.width = c.width;
    ctx.params.max_trees = c.max_trees;
    ctx.params.map_rect_w = c.w;
    ctx.params.map_rect_h = c.h;

    int ret = -1;
    {
      FpuChopScope chop;
      if (c.kind == "write") {
        WriteLatBlock(ctx, CellCoord{c.a0, c.a1}, c.a2, c.a3, c.a4, c.a5 != 0);
      } else if (c.kind == "tree") {
        ret = GrowTreePatch(ctx, CellCoord{c.a0, c.a1}, c.a2, c.prob);
      } else if (c.kind == "veg") {
        SeedVegetationProbabilities(ctx);
      } else {
        ctx.params.theater = (c.kind == "temp") ? 0 : 1;
        CreateLats(ctx);
      }
    }

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count()) << "取数个数不对";
    EXPECT_EQ(c.ret, ret) << "返回值不对";
    EXPECT_EQ(c.setuplat, world.setuplat) << "SetupLAT 次数不对";
    ASSERT_EQ(static_cast<std::size_t>(c.nterr), world.terrains.size());
    for (std::size_t i = 0; i < world.terrains.size(); ++i) {
      EXPECT_EQ(c.terr[i].name, world.terrains[i].name) << "第 " << i << " 棵树";
      EXPECT_EQ(c.terr[i].x, world.terrains[i].x) << "第 " << i << " 棵树";
      EXPECT_EQ(c.terr[i].y, world.terrains[i].y) << "第 " << i << " 棵树";
    }
    ASSERT_EQ(static_cast<std::size_t>(c.nrecalc), world.recalcs.size());
    for (std::size_t i = 0; i < world.recalcs.size(); ++i) {
      EXPECT_EQ(c.recalc[i].x, world.recalcs[i].x) << "第 " << i << " 次 Recalc";
      EXPECT_EQ(c.recalc[i].y, world.recalcs[i].y) << "第 " << i << " 次 Recalc";
      EXPECT_EQ(c.recalc[i].arg, world.recalcs[i].arg);
    }

    // 逐格比：基准只列了「跑完与跑前不同」的格子，没列的必须保持初态
    grid.ForEachCell([&](const CellCoord& cc) {
      const long long k = Key(cc.x, cc.y);
      CellState want;
      const std::map<long long, CellState>::const_iterator bi = c.in.find(k);
      if (bi != c.in.end()) want = bi->second;
      const std::map<long long, CellState>::const_iterator oi = c.out.find(k);
      if (oi != c.out.end()) {
        const CellState& o = oi->second;
        want.tile = o.tile;
        want.overlay = o.overlay;
        want.odata = o.odata;
        want.queued = o.queued;
        want.flag47 = o.flag47;
        want.rough = o.rough;
        want.green = o.green;
        want.sand = o.sand;
      }
      FakeLatWorld::Cell& cell = world.At(cc);
      const PaintWorkCell& w = work[static_cast<std::size_t>(cc.y * side + cc.x)];
      ASSERT_EQ(want.tile, cell.tile) << "(" << cc.x << "," << cc.y << ") 砖号";
      ASSERT_EQ(want.overlay, cell.overlay) << "(" << cc.x << "," << cc.y << ") overlay";
      ASSERT_EQ(want.odata, cell.odata) << "(" << cc.x << "," << cc.y << ") +0x11E";
      ASSERT_EQ(want.queued, w.queued) << "(" << cc.x << "," << cc.y << ") +0x3C";
      ASSERT_EQ(want.flag47 != 0, w.flag47) << "(" << cc.x << "," << cc.y << ") +0x47";
      ASSERT_EQ(want.rough, w.prob_rough) << "(" << cc.x << "," << cc.y << ") +0x18";
      ASSERT_EQ(want.green, w.prob_green) << "(" << cc.x << "," << cc.y << ") +0x20";
      ASSERT_EQ(want.sand, w.prob_sand) << "(" << cc.x << "," << cc.y << ") +0x28";
    });
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
