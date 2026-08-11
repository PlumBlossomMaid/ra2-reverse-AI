// 阶段 4「Making regions」五入口整链黄金对拍
//
// 基准由 code/randmap/dump_making_regions_golden.py 用 Unicorn 跑原版机器码
// 取得，存档 memory/data/rmg/making_regions_golden.txt。五入口按原版调度
// （0x598960 两条调试日志之间）逐个执行：
//   RebuildAndSplitRegions 0x58EBC0 / RebuildRegions 0x58EF10 /
//   TrimCliffCorners 0x5A19E0 / RepaintAll 0x578E60 / RerollMultitileCliffs 0x5A17F0
//
// 每个用例从 IN 行重建格子状态、从 INREG 行重建阶段 3 之后的区域表
// （种子格/层高/湿标记 + nextid），再跑五入口，比六样：
//   1) 每入口的随机数调用次数（分五次 reset_call_count 统计）
//   2) 每格终态：tile subtile level ramp overlay overlayData owner queued
//   3) 区域表：id cellCount level wet seedX seedY finalized
//   4) next_id（[0xABED14]）
//   5) 共享哑元 0xABDC50 的 +0x24 最终坐标
//
// 合成用例（syn-*）的输入是刻意设计的收敛地形：
//   rs99 —— 全干+湖+台地 rs=99：maxCells=183、干区 309 ≥ 303，触发
//           0x58D620 真分裂（剥走块 ≥101 独立成区），0x58EBC0 深度取数
//   checker —— 棋盘 UNDECIDED 4×4 干块 + GREEN 湿带：干块 ≤ maxCells(16)，
//           0 次 Split，覆盖 Rebuild/MakeConnections 常规路径
//   checker-t —— 棋盘 + 中央 6×6 台地：SeedRegion 取数路径有料
// ⚠️ 大连续干区 n < 303 会让 0x58D620 剥走块 < 101 恒并入邻区 → 死循环，
//    新用例必须先用 count 上限跑预检（见 _probe_mr3al.py 的收敛验证）。
#include "making_regions.h"
#include "cliff_work.h"      // TrimCliffCorners / RepaintAll / RerollMultitileCliffs
#include "zones.h"           // MapRect（ctx.usable 的类型）
#include "making_stub.h"     // Temperate / MakingStub（与 test_ramps 共享）

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

struct GoldenCase {
  std::string name;
  int w = 0, h = 0;
  std::uint32_t seed = 0;
  int map_type = 0, region_size = 0, accessibility = 0;
  int rng[5] = {0, 0, 0, 0, 0};
  int ret2 = 0;
  int nextid = 0, nextid_in = 0;
  CellCoord dummy;
  std::vector<std::array<int, 5>> inregs;   // id level wet seedX seedY
  std::vector<std::array<int, 10>> in;      // x y tile sub lvl ramp ov ovd own q
  std::vector<std::array<int, 10>> out;
  std::vector<std::array<int, 7>> regs;     // id cc lvl wet sx sy fin
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
      std::string(RA2_GOLDEN_DIR) + "/making_regions_golden.txt";
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
        if (tok == "->" || Has(tok, "players=") || Has(tok, "water=") ||
            Has(tok, "usable=") || Has(tok, "tiles=") ||
            Has(tok, "terrain=") || Has(tok, "mt=")) {
          continue;
        } else if (Has(tok, "seed=")) {
          c.seed = static_cast<std::uint32_t>(std::strtoul(
              tok.c_str() + 5, nullptr, 16));
        } else if (Has(tok, "rs=")) {
          c.region_size = NamedInt(tok, "rs=");
        } else if (Has(tok, "acc=")) {
          c.accessibility = NamedInt(tok, "acc=");
        } else if (Has(tok, "rng=[")) {
          std::sscanf(tok.c_str(), "rng=[%d,%d,%d,%d,%d]", &c.rng[0],
                      &c.rng[1], &c.rng[2], &c.rng[3], &c.rng[4]);
        } else if (Has(tok, "ret2=")) {
          c.ret2 = NamedInt(tok, "ret2=");
        } else if (Has(tok, "regions=")) {
          // 防御性记录，实际用 REG 行数断言
        } else if (Has(tok, "nextid_in=")) {
          c.nextid_in = NamedInt(tok, "nextid_in=");
        } else if (Has(tok, "nextid=")) {
          c.nextid = NamedInt(tok, "nextid=");
        } else if (Has(tok, "dummy=")) {
          std::sscanf(tok.c_str(), "dummy=%d,%d", &c.dummy.x, &c.dummy.y);
        }
      }
      // mt= 单独抓（上面 continue 跳过）
      std::istringstream ss2(line);
      std::string tok2;
      while (ss2 >> tok2) {
        if (Has(tok2, "mt=")) c.map_type = NamedInt(tok2, "mt=");
      }
      out.push_back(c);
    } else if (tag == "INREG") {
      std::array<int, 5> v{};
      for (int& f : v) ss >> f;
      out.back().inregs.push_back(v);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 10> v{};
      for (int& f : v) ss >> f;
      if (tag == "IN") {
        out.back().in.push_back(v);
      } else {
        out.back().out.push_back(v);
      }
    } else if (tag == "REG") {
      std::array<int, 7> v{};
      for (int& f : v) ss >> f;
      out.back().regs.push_back(v);
    }
  }
  return out;
}

TEST(MakingRegions, MatchesOriginal) {
  const std::vector<GoldenCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();
  int split_cases = 0, conn_cases = 0;

  for (const GoldenCase& c : cases) {
    if (const char* only = std::getenv("RMG_ONLY_CASE")) {
      if (c.name != only) continue;
    }
    const MapGrid grid(c.w, c.h);
    const std::size_t n = static_cast<std::size_t>(grid.side()) * grid.side();
    std::vector<PaintWorkCell> work(n);
    grid.ForEachCell([&](const CellCoord& p) {
      work[static_cast<std::size_t>(grid.WorkIndex(p))].coord = p;
    });

    MakingStub world(grid, &work);
    for (const std::array<int, 10>& e : c.in) {
      const CellCoord p{e[0], e[1]};
      world.Seed(p, e[2], e[3], e[4], e[5], e[6], e[7]);
      PaintWorkCell& rec = work[static_cast<std::size_t>(grid.WorkIndex(p))];
      rec.blob_id = e[8];
      rec.queued = e[9];
    }

    Randomizer rng(c.seed);
    NormalDeviate normal(rng);
    RegionSet regions;
    // 阶段 3 之后的区域表（INREG）重建：Create 的 id 从 0 顺序自增，
    // 与 REGVEC 里 INREG 的顺序一致；成员表/bbox 留空（0x58EBC0 第一遍清空重塞）
    for (const std::array<int, 5>& ir : c.inregs) {
      regions.Create({ir[3], ir[4]}, ir[1], ir[2] != 0);
    }
    regions.set_next_id(c.nextid_in);

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
    ctx.fallback_level = 0;      // [0xABE2E4]，oracle 未初始化（bss 全 0）
    ctx.region_size = c.region_size;
    ctx.accessibility = c.accessibility;
    ctx.map_type = c.map_type;
    // 实测 oracle 的 [0x87F8E4..F0] = {0, 0, W, H}、[0x87F8DC] = W
    ctx.usable = Rect{0, 0, c.w, c.h};
    ctx.map_rect_w = c.w;

    int got_rng[5] = {0, 0, 0, 0, 0};
    {
      FpuChopScope fpu;   // 原版全局 RC = 向零截断（_control87(0x300,0x300)）
      rng.reset_call_count();
      RebuildAndSplitRegions(ctx);
      got_rng[0] = static_cast<int>(rng.call_count());
      rng.reset_call_count();
      RebuildRegions(ctx, world);
      got_rng[1] = static_cast<int>(rng.call_count());
      rng.reset_call_count();
      TrimCliffCorners(ctx, world);
      got_rng[2] = static_cast<int>(rng.call_count());
      rng.reset_call_count();
      RepaintAll(ctx, world);
      got_rng[3] = static_cast<int>(rng.call_count());
      rng.reset_call_count();
      RerollMultitileCliffs(ctx, world);
      got_rng[4] = static_cast<int>(rng.call_count());
    }

    if (c.rng[0] > 0) ++split_cases;
    if (c.rng[1] > 0) ++conn_cases;

    for (int i = 0; i < 5; ++i) {
      EXPECT_EQ(got_rng[i], c.rng[i])
          << "case " << c.name << " rng[" << i << "]";
    }
    EXPECT_EQ(world.dummy_coord().x, c.dummy.x) << "case " << c.name;
    EXPECT_EQ(world.dummy_coord().y, c.dummy.y) << "case " << c.name;

    // 格子终态
    int bad = 0;
    for (const std::array<int, 10>& e : c.out) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      if (got.tile == e[2] && got.subtile == e[3] &&
          static_cast<int>(got.level) == e[4] && got.ramp == e[5] &&
          got.overlay == e[6] && got.overlay_data == e[7] &&
          rec.blob_id == e[8] && rec.queued == e[9]) {
        continue;
      }
      if (++bad <= 10) {
        ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                      << ") got tile=" << got.tile << " sub=" << got.subtile
                      << " lvl=" << static_cast<int>(got.level)
                      << " ramp=" << got.ramp << " ov=" << got.overlay
                      << " ovd=" << got.overlay_data
                      << " owner=" << rec.blob_id << " queued=" << rec.queued
                      << " want tile=" << e[2] << " sub=" << e[3]
                      << " lvl=" << e[4] << " ramp=" << e[5] << " ov=" << e[6]
                      << " ovd=" << e[7] << " owner=" << e[8]
                      << " queued=" << e[9];
      }
    }
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " cells differ";

    // 区域表终态（顺序 = REGVEC）
    EXPECT_EQ(regions.Count(), static_cast<int>(c.regs.size()))
        << "case " << c.name;
    EXPECT_EQ(regions.next_id(), c.nextid) << "case " << c.name;
    int rbad = 0;
    for (std::size_t i = 0; i < c.regs.size() && i < regions.Count(); ++i) {
      const RegionClass* r = regions.At(static_cast<int>(i));
      const std::array<int, 7>& e = c.regs[i];
      if (r->id == e[0] && r->cell_count == e[1] && r->level == e[2] &&
          (r->wet ? 1 : 0) == e[3] && r->seed.x == e[4] &&
          r->seed.y == e[5] && (r->finalized ? 1 : 0) == e[6]) {
        continue;
      }
      if (++rbad <= 5) {
        ADD_FAILURE() << "case " << c.name << " region[" << i
                      << "] got id=" << r->id << " cc=" << r->cell_count
                      << " lvl=" << r->level << " wet=" << (r->wet ? 1 : 0)
                      << " seed=" << r->seed.x << "," << r->seed.y
                      << " fin=" << (r->finalized ? 1 : 0)
                      << " want id=" << e[0] << " cc=" << e[1] << " lvl=" << e[2]
                      << " wet=" << e[3] << " seed=" << e[4] << "," << e[5]
                      << " fin=" << e[6];
      }
    }
    EXPECT_EQ(rbad, 0) << "case " << c.name << ": " << rbad
                       << " regions differ";
  }

  // 覆盖面不许悄悄缩水：Split 深度路径、常规连接路径都要有
  EXPECT_EQ(cases.size(), 10u);
  EXPECT_GE(split_cases, 2);
  EXPECT_GE(conn_cases, 6);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
