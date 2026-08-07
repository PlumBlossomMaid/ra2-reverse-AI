// 装载层的回归：C++ 侧从 .mix 现读出来的砖几何，必须与仓库里那份
// memory/data/rmg/tile_geometry.txt 逐字段一致。
//
// 那份档案是 code/randmap/dump_tile_geometry.py（Python 侧、独立实现）生成的，
// 所以这条对拍是**两套独立实现互相印证**：mix 头解密、名字哈希、.tmp 头解析、
// theater ini 前缀和，四处任一出错都会被抓住。
//
// 需要环境变量 RA2_GAME_DIR 指向原版目录；没设就整段 SKIP（仓库不放游戏数据）。
#include "theater_assets.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "zones.h"   // kGroundWater / kTerrainToGround

namespace ra2 {
namespace assets {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build"
#endif

struct GoldenSub {
  int present = 0, height = 0, ramp = 0, terrain = 0;
};
struct GoldenTile {
  std::string theater, group, file;
  int number = 0, w = 0, h = 0;
  std::vector<GoldenSub> sub;
};

std::vector<GoldenTile> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/tile_geometry.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开 " << path;
  std::vector<GoldenTile> out;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "TILE") {
      GoldenTile t;
      is >> t.theater >> t.group >> t.number >> t.file >> t.w >> t.h;
      out.push_back(t);
    } else if (key == "SUB" && !out.empty()) {
      int k = 0;
      GoldenSub s;
      is >> k >> s.present >> s.height >> s.ramp >> s.terrain;
      out.back().sub.push_back(s);
    }
  }
  return out;
}

const char* GameDir() { return std::getenv("RA2_GAME_DIR"); }

TEST(TheaterAssets, MatchesPythonDump) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过装载层对拍";
  const std::vector<GoldenTile> golden = LoadGolden();
  ASSERT_FALSE(golden.empty());

  TheaterAssets tem, sno;
  std::string err;
  ASSERT_TRUE(tem.Load(dir, TheaterAssets::kTemperate, &err)) << err;
  ASSERT_TRUE(sno.Load(dir, TheaterAssets::kSnow, &err)) << err;

  // theater-data.md §四那张表
  EXPECT_EQ(838, tem.data().total_tiles);
  EXPECT_EQ(964, sno.data().total_tiles);
  EXPECT_EQ(314, tem.data().water_set);
  EXPECT_EQ(314, sno.data().water_set);
  EXPECT_EQ(89, tem.data().shore_pieces);
  EXPECT_EQ(89, sno.data().shore_pieces);
  EXPECT_EQ(493, tem.data().green_tile);
  EXPECT_EQ(713, sno.data().green_tile);
  EXPECT_EQ(0, tem.data().clear_tile);
  EXPECT_EQ(131, tem.data().rough_tile);
  // 两条硬编码宽度（IsWater 的 +0x0E、岸砖清扫的 +0x2A）
  EXPECT_EQ(14, tem.data().TilesInSetStartingAt(tem.data().water_set));
  EXPECT_EQ(42, tem.data().TilesInSetStartingAt(tem.data().shore_pieces));
  EXPECT_EQ(14, sno.data().TilesInSetStartingAt(sno.data().water_set));
  EXPECT_EQ(42, sno.data().TilesInSetStartingAt(sno.data().shore_pieces));

  int checked = 0;
  for (std::size_t i = 0; i < golden.size(); ++i) {
    const GoldenTile& g = golden[i];
    const TheaterAssets& a = g.theater == "snow" ? sno : tem;
    SCOPED_TRACE(g.theater + " " + g.group + " #" + std::to_string(g.number) +
                 " " + g.file);
    ASSERT_LT(g.number, a.TileCount());
    const TileType& t = a.Tile(g.number);
    EXPECT_EQ(g.file, t.file);
    ASSERT_TRUE(t.loaded);
    EXPECT_EQ(g.w, t.blocks_x);
    EXPECT_EQ(g.h, t.blocks_y);
    ASSERT_EQ(g.sub.size(), t.sub.size());
    for (std::size_t k = 0; k < g.sub.size(); ++k) {
      EXPECT_EQ(g.sub[k].present != 0, t.sub[k].present) << "sub " << k;
      EXPECT_EQ(g.sub[k].height, static_cast<int>(t.sub[k].height)) << "sub " << k;
      EXPECT_EQ(g.sub[k].ramp, static_cast<int>(t.sub[k].ramp)) << "sub " << k;
      EXPECT_EQ(g.sub[k].terrain, static_cast<int>(t.sub[k].terrain)) << "sub " << k;
    }
    ++checked;
  }
  EXPECT_GT(checked, 100);
}

// 缺文件的砖：BlocksX/BlocksY 必须是 0（0x546B93），且取地面类别返回 0。
// 温和有 20 个组共 343 块砖是这种情况——全是 ZMM / Obsolete 之类的编辑器素材。
TEST(TheaterAssets, MissingTilesAreZeroSized) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过装载层对拍";
  TheaterAssets tem;
  std::string err;
  ASSERT_TRUE(tem.Load(dir, TheaterAssets::kTemperate, &err)) << err;
  int missing = 0;
  for (int i = 0; i < tem.TileCount(); ++i) {
    if (tem.Tile(i).loaded) continue;
    ++missing;
    EXPECT_EQ(0, tem.Tile(i).blocks_x) << tem.Tile(i).file;
    EXPECT_EQ(0, tem.Tile(i).blocks_y) << tem.Tile(i).file;
    EXPECT_EQ(0, tem.GroundTypeOf(i, 0)) << tem.Tile(i).file;
  }
  EXPECT_EQ(343, missing);
}

// 0x544BE0 的取模：子格下标越界会绕回去，不越界访问。
TEST(TheaterAssets, GroundTypeWrapsSubIndex) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过装载层对拍";
  TheaterAssets tem;
  std::string err;
  ASSERT_TRUE(tem.Load(dir, TheaterAssets::kTemperate, &err)) << err;
  const int water = tem.data().water_set;
  for (int k = 0; k < 14; ++k) {
    const TileType& t = tem.Tile(water + k);
    const int n = static_cast<int>(t.sub.size());
    ASSERT_GT(n, 0);
    for (int s = 0; s < n; ++s) {
      EXPECT_EQ(tem.GroundTypeOf(water + k, s),
                tem.GroundTypeOf(water + k, s + n * 3));
    }
  }
  // 水砖子格的 TerrainType 是 9，kTerrainToGround[9] = 2 = kGroundWater
  EXPECT_EQ(rmg::kGroundWater, tem.GroundTypeOf(water, 0));
  // 空砖（clear01）的 TerrainType 是 13 -> 0 = kGroundClear
  EXPECT_EQ(rmg::kGroundClear, tem.GroundTypeOf(tem.data().clear_tile, 0));
}

// 转成 rmg::TileType 之后，贴砖器要的三样必须一一对上；缺文件的砖是空壳。
TEST(TheaterAssets, RmgTileTable) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过装载层对拍";
  TheaterAssets tem;
  std::string err;
  ASSERT_TRUE(tem.Load(dir, TheaterAssets::kTemperate, &err)) << err;
  std::vector<rmg::TileType> table;
  tem.BuildRmgTileTable(&table);
  ASSERT_EQ(static_cast<std::size_t>(tem.TileCount()), table.size());
  for (int i = 0; i < tem.TileCount(); ++i) {
    const TileType& src = tem.Tile(i);
    const rmg::TileType& dst = table[static_cast<std::size_t>(i)];
    ASSERT_EQ(i, dst.tile_number);
    ASSERT_EQ(src.blocks_x, dst.width);
    ASSERT_EQ(src.blocks_y, dst.height);
    ASSERT_EQ(src.sub.size(), dst.cells.size());
    if (!src.loaded) {
      EXPECT_EQ(0, dst.width);
      EXPECT_EQ(0, dst.height);
      EXPECT_TRUE(dst.cells.empty());
      continue;
    }
    ASSERT_EQ(static_cast<std::size_t>(dst.width * dst.height), dst.cells.size());
    for (std::size_t k = 0; k < dst.cells.size(); ++k) {
      EXPECT_EQ(src.sub[k].present, dst.cells[k].present);
      EXPECT_EQ(static_cast<int>(src.sub[k].height), dst.cells[k].height);
      EXPECT_EQ(static_cast<int>(src.sub[k].ramp), dst.cells[k].ramp);
      EXPECT_EQ(static_cast<int>(src.sub[k].terrain), dst.cells[k].terrain_type);
    }
  }
  // 岸砖那 42 块是贴砖器真正会用到的，尺寸必须都是有效的
  const int shore = tem.data().shore_pieces;
  for (int k = 0; k < 42; ++k) {
    const rmg::TileType& t = table[static_cast<std::size_t>(shore + k)];
    EXPECT_GT(t.width, 0) << "shore+" << k;
    EXPECT_GT(t.height, 0) << "shore+" << k;
  }
}

}  // namespace
}  // namespace assets
}  // namespace ra2
