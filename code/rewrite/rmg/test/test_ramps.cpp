// 坡道建造函数六连（四直坡 + 三转角）的逐字节对拍。
//
// 基准由 code/randmap/dump_ramps_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/ramps_golden.txt。每个用例直接调用原版建造函数
// （不经 0x590970 八分支调度）：
//   BuildStraightSouth 0x5910F0 / East 0x591740 / North 0x591D80 /
//   West 0x592440、BuildCornerNE 0x593AF0 / ES 0x593550 / SW 0x593030
// 输入是 30×30 全图（tile=0xFFFF 平地 + 净空矩形内 owner=low）+ 区域表
// （hi id=1 level=4、low id=0 level=0）。比三样：
//   1) 返回值 ret
//   2) RNG 消耗（直坡 dx/dy≠0 才取 1 个硬币，转角恒 0）
//   3) 终态：OUT 记录的变化格必须逐字段一致，且**未变化格必须保持输入**
//      （双向约束，防止多写/漏写）
//
// ⚠️ oracle 侧环境四件套（side = w+h+1 = 61 的 work 数组、砖表实体化、
//    [0xABC1D8]=29/[0xABC1F8]=200 等 TheaterClass 全局）已在生成脚本里
//    摆好；C++ 侧由 MakingStub + Temperate() 提供等价物。
#include "making_regions.h"
#include "ramps.h"
#include "regions.h"
#include "making_stub.h"     // Temperate / MakingStub（与 test_making_regions 共享）

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

struct RampsCase {
  std::string name;
  int w = 0, h = 0;
  std::uint32_t seed = 0;
  int hi = 0, hi_lvl = 0, low = 0;
  CellCoord a, b;
  int rng = 0, ret = 0;
  std::vector<std::array<int, 10>> in;    // 全图（菱形 1770 格）
  std::vector<std::array<int, 10>> out;   // 变化格
};

int NamedInt(const std::string& tok, const char* key) {
  const std::size_t n = std::strlen(key);
  EXPECT_EQ(tok.compare(0, n, key), 0) << "expected " << key << " got " << tok;
  return std::atoi(tok.c_str() + n);
}

bool Has(const std::string& tok, const char* key) {
  return tok.compare(0, std::strlen(key), key) == 0;
}

std::vector<RampsCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/ramps_golden.txt";
  std::ifstream in(path);
  EXPECT_TRUE(in.good()) << "cannot open " << path;
  std::vector<RampsCase> out;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "CASE") {
      RampsCase c;
      ss >> c.name >> c.w >> c.h;
      std::string tok;
      while (ss >> tok) {
        if (Has(tok, "seed=")) {
          c.seed = static_cast<std::uint32_t>(
              std::strtoul(tok.c_str() + 5, nullptr, 16));
        } else if (Has(tok, "hi_lvl=")) {
          c.hi_lvl = NamedInt(tok, "hi_lvl=");
        } else if (Has(tok, "hi=")) {
          c.hi = NamedInt(tok, "hi=");
        } else if (Has(tok, "low=")) {
          c.low = NamedInt(tok, "low=");
        } else if (Has(tok, "a=")) {
          std::sscanf(tok.c_str(), "a=%d,%d", &c.a.x, &c.a.y);
        } else if (Has(tok, "b=")) {
          std::sscanf(tok.c_str(), "b=%d,%d", &c.b.x, &c.b.y);
        } else if (Has(tok, "rng=")) {
          c.rng = NamedInt(tok, "rng=");
        } else if (Has(tok, "ret=")) {
          c.ret = NamedInt(tok, "ret=");
        }
      }
      out.push_back(c);
    } else if (tag == "IN" || tag == "OUT") {
      std::array<int, 10> v{};
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

// CASE 名 → 原版建造函数
using Builder = bool (*)(RegionContext&, MakingWorld&, RegionClass*,
                         const CellCoord&, const CellCoord&, int);

Builder BuilderOf(const std::string& name) {
  if (name.rfind("east", 0) == 0) return BuildStraightEast;
  if (name.rfind("north", 0) == 0) return BuildStraightNorth;
  if (name.rfind("west", 0) == 0) return BuildStraightWest;
  if (name == "corner-ne") return BuildCornerNE;
  if (name == "corner-es") return BuildCornerES;
  if (name == "corner-sw") return BuildCornerSW;
  return nullptr;
}

TEST(Ramps, MatchesOriginal) {
  const std::vector<RampsCase> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  const TheaterData theater = Temperate();

  for (const RampsCase& c : cases) {
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
    regions.Create({1, 1}, 0, false);          // id=0 低区（level 0）
    RegionClass* hi = regions.Create({1, 1}, c.hi_lvl, false);  // id=1 高区
    ASSERT_EQ(hi->id, c.hi);
    RegionClass* low = regions.At(0);
    ASSERT_EQ(low->id, c.low);

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
    ctx.fallback_level = 0;
    ctx.region_size = 0;
    ctx.accessibility = 90;
    ctx.map_type = 3;
    ctx.usable = Rect{0, 0, c.w, c.h};
    ctx.map_rect_w = c.w;

    bool got_ret = false;
    {
      FpuChopScope fpu;   // 原版全局 RC = 向零截断
      rng.reset_call_count();
      Builder build = BuilderOf(c.name);
      ASSERT_NE(build, nullptr) << "case " << c.name;
      got_ret = build(ctx, world, hi, c.a, c.b, c.low);
    }

    EXPECT_EQ(got_ret, c.ret != 0) << "case " << c.name << " ret";
    EXPECT_EQ(static_cast<int>(rng.call_count()), c.rng)
        << "case " << c.name << " rng";

    // 双向约束：OUT 变化格必须一致，未变化格必须保持输入
    int bad = 0, changed = 0;
    for (const std::array<int, 10>& e : c.in) {
      const CellCoord q{e[0], e[1]};
      const Cell got = world.Get(q);
      const PaintWorkCell& rec =
          work[static_cast<std::size_t>(grid.WorkIndex(q))];
      const std::array<int, 10> cur{e[0], e[1], got.tile, got.subtile,
                                    static_cast<int>(got.level), got.ramp,
                                    got.overlay, got.overlay_data, rec.blob_id,
                                    rec.queued};
      const bool is_changed =
          !(cur[2] == e[2] && cur[3] == e[3] && cur[4] == e[4] &&
            cur[5] == e[5] && cur[6] == e[6] && cur[7] == e[7] &&
            cur[8] == e[8] && cur[9] == e[9]);
      if (is_changed) ++changed;
      // OUT 里有没有这格
      bool in_out = false;
      for (const std::array<int, 10>& o : c.out) {
        if (o[0] == e[0] && o[1] == e[1]) {
          in_out = true;
          if (cur != o) {
            if (++bad <= 10) {
              ADD_FAILURE() << "case " << c.name << " cell (" << q.x << ","
                            << q.y << ") got " << cur[2] << "/" << cur[3]
                            << "/" << cur[4] << "/" << cur[5] << "/" << cur[6]
                            << "/" << cur[7] << "/" << cur[8] << "/" << cur[9]
                            << " want " << o[2] << "/" << o[3] << "/" << o[4]
                            << "/" << o[5] << "/" << o[6] << "/" << o[7]
                            << "/" << o[8] << "/" << o[9];
            }
          }
          break;
        }
      }
      if (!in_out && is_changed) {
        if (++bad <= 10) {
          ADD_FAILURE() << "case " << c.name << " cell (" << q.x << "," << q.y
                        << ") changed but not in OUT (" << cur[2] << "/"
                        << cur[3] << "/" << cur[4] << "/" << cur[5] << "/"
                        << cur[6] << "/" << cur[7] << "/" << cur[8] << "/"
                        << cur[9] << ")";
        }
      }
    }
    EXPECT_EQ(changed, static_cast<int>(c.out.size()))
        << "case " << c.name << " changed-cell count";
    EXPECT_EQ(bad, 0) << "case " << c.name << ": " << bad << " mismatches";
  }

  // 覆盖面不许缩水：直坡与转角都要有
  EXPECT_EQ(cases.size(), 9u);
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
