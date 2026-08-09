// 阶段 6「Creating starting points」整段（0x594B50）的黄金对拍。
//
// 与 test_start_points.cpp 分工：那边只对最远点采样 0x594F40 这一个零件，
// 这边把**调度 + 起区域 + 逐区域摆点**连成一整条跟原版机器码对拍。
// 基准由 code/randmap/dump_start_points2_golden.py 用 Unicorn 取得，
// 存档 memory/data/rmg/start_points2_golden.txt；那一份只打了四个桩
// （0x56C510 重建寻路分区 / 0x56D230 查移动区类型 / 0x595400 摆建筑 /
//  0x58B820 收尾同步），其余包括 0x5A7250、0x578640、qsort 与比较器
// 全跑真身，所以下面每一条都是「原版怎么走就得怎么走」。
//
// 逐例比七样：
//   1) 随机数次数（三个取数点一个都不能多抽少抽）
//   2) 返回值（原版所有出口都 return 1）
//   3) 区域表：顺序、id、格数、名额、种子、成员格表、候选点表
//   4) 工作数组每格的 +0x38 归属 / +0x3C 入队标记
//   5) waypoint 表
//   6) ScenarioClass 的出生点表（那条「每个区域都重置一遍」的原版 bug）
//   7) CellClass +0x140 的 bit2 与 PlaceRandomBuilding 的调用序列
#include "start_points.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"   // FpuChopScope

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// 基准档头一行 `# PavedRoads=300 ...` 里的合成砖号基址。
// 真实气候里 PaveTile / MiscPaveTile 都是 -1，那样 0x486650 会把小砖号全判成
// 「铺装」，压不出 0x5A7250 的拒绝分支，所以基准用的是这四个合成值。
TheaterData SyntheticTheater() {
  TheaterData t;
  t.paved_roads = 300;
  t.paved_road_ends = 340;
  t.pave_tile = 600;
  t.misc_pave_tile = 700;
  return t;
}

constexpr int kCellSlots = 0x40000;   // 0x5657A0 的下标区间 [0, 0x40000)

int SlotOf(const CellCoord& c) {
  const int idx = (c.y << 9) + c.x;
  return (idx < 0 || idx >= kCellSlots) ? -1 : idx;
}

struct Cell {
  int zone_id = 0;        // +0x4C
  int mzt = -1;           // 0x56D230 的返回值（基准里按格给定）
  int tile = 0;           // +0x38
  std::int8_t level = 0;  // +0x11B
  int ramp = 0;           // +0x11C
  bool flagged = false;   // +0x140 的 bit2
};

class StartStub : public StartPointWorld {
 public:
  StartStub() : cells_(kCellSlots) {}

  int CellZoneId(const CellCoord& c) const override { return At(c).zone_id; }
  int MovementZoneType(const CellCoord& c, int) const override {
    return At(c).mzt;
  }
  int IsoTile(const CellCoord& c) const override { return At(c).tile; }
  int Level(const CellCoord& c) const override { return At(c).level; }
  int Ramp(const CellCoord& c) const override { return At(c).ramp; }

  // 0x5657A0 拿不到实体格时把请求坐标写进共享哑元的 +0x24，随后读回来的
  // 就是刚写进去的那一份 —— 于是回读值恒等于请求坐标本身。
  CellCoord ReadBackCoord(const CellCoord& c) override {
    if (SlotOf(c) < 0) dummy_coord_ = c;
    return c;
  }

  void SetWaypoint(int index, const CellCoord& c) override {
    if (index >= 0 && index < static_cast<int>(waypoints_.size())) {
      waypoints_[static_cast<std::size_t>(index)] = c;
    }
    Mut(c).flagged = true;      // 0x594AC5：cell +0x140 |= 4
  }
  void ResetStartLocationTable() override {
    for (std::size_t i = 0; i < start_locs_.size(); ++i) {
      start_locs_[i] = 0x7FFF7FFFu;      // [0xABE300]
    }
  }
  void SetStartLocation(int slot, const CellCoord& c) override {
    if (slot >= 0 && slot < static_cast<int>(start_locs_.size())) {
      start_locs_[static_cast<std::size_t>(slot)] = c.Packed();
    }
  }
  void PlaceRandomBuilding(RegionClass* r) override {
    buildings_.push_back(r->id);
  }

  void Seed(const CellCoord& c, const Cell& v) {
    const int s = SlotOf(c);
    if (s >= 0) cells_[static_cast<std::size_t>(s)] = v;
  }
  bool Flagged(const CellCoord& c) const { return At(c).flagged; }
  const std::vector<CellCoord>& waypoints() const { return waypoints_; }
  const std::vector<std::uint32_t>& start_locs() const { return start_locs_; }
  const std::vector<int>& buildings() const { return buildings_; }

 private:
  const Cell& At(const CellCoord& c) const {
    const int s = SlotOf(c);
    return s < 0 ? dummy_ : cells_[static_cast<std::size_t>(s)];
  }
  Cell& Mut(const CellCoord& c) {
    const int s = SlotOf(c);
    return s < 0 ? dummy_ : cells_[static_cast<std::size_t>(s)];
  }

  std::vector<Cell> cells_;
  Cell dummy_;
  CellCoord dummy_coord_;
  std::vector<CellCoord> waypoints_ = std::vector<CellCoord>(64);
  std::vector<std::uint32_t> start_locs_ = std::vector<std::uint32_t>(8, 0);
  std::vector<int> buildings_;
};

struct GoldenRegion {
  int id = 0, count = 0, start = 0, seed_x = 0, seed_y = 0;
  int n_cells = 0, n_cand = 0;
  std::vector<CellCoord> cells, cand;
};

struct GoldenCase {
  std::string name, spec;
  int w = 0, h = 0, players = 2, map_type = 0, scale = 0;
  unsigned seed = 0;
  int ret = 0, regions = 0, next_id = 0;
  long long rng = 0;
  std::vector<std::vector<int> > in;      // x y zoneid mzt tile level ramp
  std::vector<GoldenRegion> reg;
  std::vector<std::vector<int> > work;    // x y owner queued
  std::vector<CellCoord> wp;
  std::vector<std::uint32_t> sl;
  std::vector<CellCoord> flags;
  std::vector<int> bld;
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

// "12:34,56:78" -> 点表；"-" 表示空
std::vector<CellCoord> ParsePoints(const std::string& body) {
  std::vector<CellCoord> out;
  if (body == "-") return out;
  std::size_t i = 0;
  while (i < body.size()) {
    std::size_t comma = body.find(',', i);
    if (comma == std::string::npos) comma = body.size();
    const std::string item = body.substr(i, comma - i);
    const std::size_t colon = item.find(':');
    CellCoord p;
    p.x = std::atoi(item.substr(0, colon).c_str());
    p.y = std::atoi(item.substr(colon + 1).c_str());
    out.push_back(p);
    i = comma + 1;
  }
  return out;
}

std::vector<int> ParseInts(const std::string& body) {
  std::vector<int> out;
  if (body == "-") return out;
  std::size_t i = 0;
  while (i < body.size()) {
    std::size_t comma = body.find(',', i);
    if (comma == std::string::npos) comma = body.size();
    out.push_back(std::atoi(body.substr(i, comma - i).c_str()));
    i = comma + 1;
  }
  return out;
}

std::vector<GoldenCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/start_points2_golden.txt";
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot open " << path;
  std::vector<GoldenCase> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      GoldenCase c;
      std::string wtok, htok, seedtok, ptok, mttok, sctok, spectok, arrow,
          rettok, rngtok, regtok, nidtok;
      ss >> c.name >> wtok >> htok >> seedtok >> ptok >> mttok >> sctok
         >> spectok >> arrow >> rettok >> rngtok >> regtok >> nidtok;
      c.w = NamedInt(wtok, "w=");
      c.h = NamedInt(htok, "h=");
      c.seed = static_cast<unsigned>(
          std::strtoul(seedtok.c_str() + std::strlen("seed=0x"), 0, 16));
      c.players = NamedInt(ptok, "players=");
      c.map_type = NamedInt(mttok, "mt=");
      c.scale = NamedInt(sctok, "scale=");
      c.spec = spectok.substr(std::strlen("spec="));
      c.ret = NamedInt(rettok, "ret=");
      c.rng = NamedInt(rngtok, "rng=");
      c.regions = NamedInt(regtok, "regions=");
      c.next_id = NamedInt(nidtok, "nextid=");
      out.push_back(c);
      continue;
    }
    if (out.empty()) continue;
    GoldenCase& c = out.back();
    if (tag == "IN") {
      std::vector<int> v(7);
      for (std::size_t k = 0; k < v.size(); ++k) ss >> v[k];
      c.in.push_back(v);
    } else if (tag == "REG") {
      GoldenRegion r;
      ss >> r.id >> r.count >> r.start >> r.seed_x >> r.seed_y >> r.n_cells
         >> r.n_cand;
      c.reg.push_back(r);
    } else if (tag == "CAND") {
      std::string body;
      ss >> body;
      c.reg.back().cand = ParsePoints(body);
    } else if (tag == "CELLS") {
      std::string body;
      ss >> body;
      c.reg.back().cells = ParsePoints(body);
    } else if (tag == "WORK") {
      std::vector<int> v(4);
      for (std::size_t k = 0; k < v.size(); ++k) ss >> v[k];
      c.work.push_back(v);
    } else if (tag == "WP") {
      int i = 0, x = 0, y = 0;
      ss >> i >> x >> y;
      CellCoord p;
      p.x = x;
      p.y = y;
      c.wp.push_back(p);
    } else if (tag == "SL") {
      int i = 0;
      unsigned long v = 0;
      ss >> i >> v;
      c.sl.push_back(static_cast<std::uint32_t>(v));
    } else if (tag == "FLAG") {
      int x = 0, y = 0;
      ss >> x >> y;
      CellCoord p;
      p.x = x;
      p.y = y;
      c.flags.push_back(p);
    } else if (tag == "BLD") {
      std::string body;
      ss >> body;
      c.bld = ParseInts(body);
    }
  }
  return out;
}

TEST(PlaceStartPoints, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_EQ(17u, cases.size());
  const TheaterData theater = SyntheticTheater();

  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const GoldenCase& c = cases[ci];
    SCOPED_TRACE(c.name);
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    // 基准那边刻意把整张工作数组铺成 0x5A5A5A5A，好让「漏掉 0x58CE90 的整表
    // 刷 -1」当场露馅。这里照样铺。
    std::vector<PaintWorkCell> work(n);
    for (std::size_t i = 0; i < n; ++i) {
      work[i].blob_id = 0x5A5A5A5A;
      work[i].queued = 0x5A5A5A5A;
    }

    StartStub world;
    for (std::size_t i = 0; i < c.in.size(); ++i) {
      const std::vector<int>& e = c.in[i];
      Cell v;
      v.zone_id = e[2];
      v.mzt = e[3];
      v.tile = e[4];
      v.level = static_cast<std::int8_t>(e[5]);
      v.ramp = e[6];
      CellCoord p;
      p.x = e[0];
      p.y = e[1];
      world.Seed(p, v);
    }

    Randomizer rng(c.seed);
    RegionSet regions;
    StartPointContext ctx;
    ctx.grid = grid;
    ctx.side = grid.side();
    ctx.work = &work;
    ctx.regions = &regions;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.theater = &theater;
    ctx.num_players = c.players;
    ctx.map_type = c.map_type;
    ctx.waypoint_scale = c.scale;
    ctx.map_w = c.w;
    ctx.map_h = c.h;
    ctx.map_rect_w = c.w;             // MapClass +0xF4
    ctx.movement_zone = kStartZoneSel[c.map_type & 7];
    ctx.want_zone_type = 3;           // 基准档头的 wantZoneType
    // 0x594876：{[0x87F8E4]+4, [0x87F8E8]+4, [0x87F8EC]-8, [0x87F8F0]-8}
    ctx.usable.x = 4;
    ctx.usable.y = 4;
    ctx.usable.w = c.w - 8;
    ctx.usable.h = c.h - 8;

    rng.reset_call_count();
    bool ret = false;
    {
      FpuChopScope chop;             // 三处取数与名额分配都走 _ftol（截断）
      ret = PlaceStartPoints(ctx);
    }

    EXPECT_EQ(ret ? 1 : 0, c.ret);
    EXPECT_EQ(rng.call_count(), c.rng);
    EXPECT_EQ(regions.Count(), c.regions);
    EXPECT_EQ(regions.next_id(), c.next_id);

    ASSERT_EQ(c.reg.size(), static_cast<std::size_t>(regions.Count()));
    for (std::size_t i = 0; i < c.reg.size(); ++i) {
      const GoldenRegion& e = c.reg[i];
      const RegionClass& r = *regions.At(static_cast<int>(i));
      SCOPED_TRACE(i);
      EXPECT_EQ(r.id, e.id);
      EXPECT_EQ(r.cell_count, e.count);
      EXPECT_EQ(r.start_points, e.start);
      EXPECT_EQ(r.seed.x, e.seed_x);
      EXPECT_EQ(r.seed.y, e.seed_y);
      ASSERT_EQ(r.cells.size(), static_cast<std::size_t>(e.n_cells));
      ASSERT_EQ(e.cells.size(), r.cells.size());
      for (std::size_t k = 0; k < r.cells.size(); ++k) {
        if (r.cells[k].x != e.cells[k].x || r.cells[k].y != e.cells[k].y) {
          ADD_FAILURE() << "成员格 " << k << " 应为 (" << e.cells[k].x << ","
                        << e.cells[k].y << ") 实为 (" << r.cells[k].x << ","
                        << r.cells[k].y << ")";
          break;
        }
      }
      ASSERT_EQ(r.candidates.size(), static_cast<std::size_t>(e.n_cand));
      ASSERT_EQ(e.cand.size(), r.candidates.size());
      for (std::size_t k = 0; k < r.candidates.size(); ++k) {
        EXPECT_EQ(r.candidates[k].x, e.cand[k].x) << "候选 " << k;
        EXPECT_EQ(r.candidates[k].y, e.cand[k].y) << "候选 " << k;
      }
    }

    // 工作数组：基准只列了 (owner, queued) != (-1, -1) 的格子，其余必须是 -1
    std::set<int> listed;
    int bad = 0;
    for (std::size_t i = 0; i < c.work.size(); ++i) {
      const std::vector<int>& e = c.work[i];
      CellCoord p;
      p.x = e[0];
      p.y = e[1];
      const int idx = grid.WorkIndex(p);
      listed.insert(idx);
      const PaintWorkCell& w = work[static_cast<std::size_t>(idx)];
      if (w.blob_id == e[2] && w.queued == e[3]) continue;
      if (++bad <= 10) {
        ADD_FAILURE() << "工作记录 (" << p.x << "," << p.y << ") 应为 " << e[2]
                      << "/" << e[3] << " 实为 " << w.blob_id << "/" << w.queued;
      }
    }
    grid.ForEachCell([&](const CellCoord& p) {
      const int idx = grid.WorkIndex(p);
      if (listed.count(idx) != 0) return;
      const PaintWorkCell& w = work[static_cast<std::size_t>(idx)];
      if (w.blob_id == -1 && w.queued == -1) return;
      if (++bad <= 10) {
        ADD_FAILURE() << "工作记录 (" << p.x << "," << p.y
                      << ") 应为 -1/-1 实为 " << w.blob_id << "/" << w.queued;
      }
    });
    EXPECT_EQ(bad, 0);

    for (std::size_t i = 0; i < c.wp.size(); ++i) {
      EXPECT_EQ(world.waypoints()[i].x, c.wp[i].x) << "waypoint " << i;
      EXPECT_EQ(world.waypoints()[i].y, c.wp[i].y) << "waypoint " << i;
    }
    ASSERT_EQ(c.sl.size(), world.start_locs().size());
    for (std::size_t i = 0; i < c.sl.size(); ++i) {
      EXPECT_EQ(world.start_locs()[i], c.sl[i]) << "出生点表 " << i;
    }

    std::set<std::pair<int, int> > want_flags;
    for (std::size_t i = 0; i < c.flags.size(); ++i) {
      const CellCoord& p = c.flags[i];
      want_flags.insert(std::make_pair(p.x, p.y));
      EXPECT_TRUE(world.Flagged(p)) << "(" << p.x << "," << p.y << ") 该被标记";
    }
    grid.ForEachCell([&](const CellCoord& p) {
      if (want_flags.count(std::make_pair(p.x, p.y)) != 0) return;
      EXPECT_FALSE(world.Flagged(p))
          << "(" << p.x << "," << p.y << ") 不该被标记";
    });

    ASSERT_EQ(c.bld.size(), world.buildings().size());
    for (std::size_t i = 0; i < c.bld.size(); ++i) {
      EXPECT_EQ(world.buildings()[i], c.bld[i]) << "建筑 " << i;
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
