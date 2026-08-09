// 对拍夹具的**砖尺寸剖面**，与 code/randmap/tile_profiles.py 一一对应。
//
//   mix  —— 岸砖 1 + k%2 宽、1 + (k/2)%2 高，四种尺寸都出现，
//           故意让砖盖住右/下邻格，压出贴砖器"整块否定"的路径
//   unit —— 一律 1x1，谁也遮不住谁，纯对角分支才跑得到
//   real —— 直接读 memory/data/rmg/tile_geometry.txt（真实 .tmp 文件头）
//
// real 表由 code/randmap/dump_tile_geometry.py 从 theater 的 .tmp 里取出，
// 只含几何数值（占地宽高 / 空洞 / 高度增量 / RampType），不含任何像素资产。
// 温和与雪地这 56 块砖的几何完全相同，故只存一份。
//
// ⚠️ 三套都得留着：真实岸砖的高度增量与 RampType **全是 0**，
// 且只有 shore41/42 有空洞（而那两块砖岸线平滑根本选不到，i 最大 0x28
// 对应 shore40），所以真实数据压不出"层高累加"与"空洞跳过"两条分支。
#ifndef RA2_RMG_TEST_TILE_GEOMETRY_FIXTURE_H_
#define RA2_RMG_TEST_TILE_GEOMETRY_FIXTURE_H_

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "place_tile.h"

namespace ra2 {
namespace rmg {
namespace fixture {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build"
#endif

struct TileGeom {
  int w = 0;
  int h = 0;
  std::vector<TileSubCell> cells;
};

// key = "<group>:<组内序号>"，组内序号 = tile_number - 组起始砖号
inline const std::map<std::string, TileGeom>& RealGeometry() {
  static const std::map<std::string, TileGeom> table = [] {
    std::map<std::string, TileGeom> out;
    const std::string path = std::string(RA2_GOLDEN_DIR) + "/tile_geometry.txt";
    std::ifstream in(path);
    std::map<std::string, int> base;
    std::string line, key;
    bool keep = false;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      std::istringstream ss(line);
      std::string tag;
      ss >> tag;
      if (tag == "TILE") {
        std::string theater, group, file;
        int num = 0, w = 0, h = 0;
        ss >> theater >> group >> num >> file >> w >> h;
        keep = theater == "temperate";     // 两气候几何相同，取一份
        if (!keep) continue;
        if (!base.count(group)) base[group] = num;
        key = group + ":" + std::to_string(num - base[group]);
        out[key] = TileGeom{w, h, {}};
      } else if (tag == "SUB" && keep) {
        int k = 0, present = 0, height = 0, ramp = 0;
        ss >> k >> present >> height >> ramp;
        out[key].cells.push_back(
            TileSubCell{present != 0, height, ramp});
      }
    }
    return out;
  }();
  return table;
}

inline TileGeom ShoreGeom(int k, const std::string& profile) {
  if (profile == "real") return RealGeometry().at("shore:" + std::to_string(k));
  TileGeom g;
  g.w = profile == "unit" ? 1 : 1 + (k % 2);
  g.h = profile == "unit" ? 1 : 1 + ((k / 2) % 2);
  g.cells.assign(static_cast<std::size_t>(g.w) * g.h, TileSubCell{true, 0, 0});
  return g;
}

inline TileGeom WaterGeom(int k, const std::string& profile) {
  if (profile == "real") return RealGeometry().at("water:" + std::to_string(k));
  TileGeom g;
  g.w = k < 8 ? 2 : 1;
  g.h = k < 8 ? 2 : 1;
  // 合成剖面的 RampType 取非零值，好让 CellClass +0x11C 的写入可观测；
  // 真实数据里它全是 0。与 tile_profiles.py 的 water_spec 逐字一致。
  for (int j = 0; j < g.w * g.h; ++j) {
    g.cells.push_back(TileSubCell{true, 0, 1 + (k * 5 + j * 3) % 17});
  }
  return g;
}

inline TileType MakeTile(int tile_number, const TileGeom& g) {
  TileType t;
  t.tile_number = tile_number;
  t.width = g.w;
  t.height = g.h;
  t.cells = g.cells;
  return t;
}

}  // namespace fixture
}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_TEST_TILE_GEOMETRY_FIXTURE_H_
