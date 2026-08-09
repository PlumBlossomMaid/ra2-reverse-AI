// theater ini 解析（TileSet 枚举 -> 起始砖号）的单测
//
// 期望值来自 code/randmap/dump_tileset_starts.py 对真实
// TEMPERATMD.INI / SNOWMD.INI 的计算结果（memory/data/rmg/tileset_starts.txt）。
// ⚠️ 仓库里不放 theater 素材，所以这里用一段结构相同的**手写** ini 验算法，
// 再用两条硬编码常量做交叉验证——那两条是从 exe 机器码里读出来的：
//   IsWater 0x485060 的区间宽度 0x0E  == WaterSet 的 TilesInSet
//   涂块收尾岸砖清扫的 0x2A           == ShorePieces 的 TilesInSet
#include "theater.h"

#include <string>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

// 结构与真 ini 一致：General 给组序号，TileSetNNNN 给组内砖数。
// 砖数刻意选成与真 temperate 前几组相同，好让前缀和可以手算核对。
const char kIni[] =
    "; a comment line\n"
    "[General]\n"
    "ClearTile = 0\n"
    "ShorePieces = 3    ; trailing comment\n"
    "WaterSet = 4\n"
    "GreenTile = 5\n"
    "Rocks = 99\n"        // 指向不存在的组 -> 应得 -1
    "\n"
    "[TileSet0000]\n"
    "SetName = Lat Grass\n"
    "FileName = Clear\n"
    "TilesInSet = 1\n"
    "[TileSet0001]\n"
    "SetName = Misc Buildings\n"
    "TilesInSet = 0\n"
    "[TileSet0002]\n"
    "TilesInSet = 20\n"
    "[TileSet0003]\n"
    "SetName = Shore Pieces\n"
    "TilesInSet = 42\n"
    "[TileSet0004]\n"
    "SetName = Water\n"
    "TilesInSet = 14\n"
    "[TileSet0005]\n"
    "SetName = LAT Sand\n"
    "TilesInSet = 1\n";

TheaterData Parse() {
  TheaterData t;
  EXPECT_TRUE(ParseTheaterIni(kIni, &t));
  return t;
}

// 起始砖号是一条前缀和；0 号组从 0 起。
TEST(Theater, StartsArePrefixSums) {
  const TheaterData t = Parse();
  ASSERT_EQ(t.sets.size(), 6u);
  EXPECT_EQ(t.sets[0].start, 0);
  EXPECT_EQ(t.sets[1].start, 1);    // 前面 1 块
  EXPECT_EQ(t.sets[2].start, 1);    // 上一组 0 块，起点不动
  EXPECT_EQ(t.sets[3].start, 21);
  EXPECT_EQ(t.sets[4].start, 63);
  EXPECT_EQ(t.sets[5].start, 77);
  EXPECT_EQ(t.total_tiles, 78);
}

// General 里给的是**组序号**，全局里存的是该组的**起始砖号**——两者不是一回事。
TEST(Theater, GlobalsHoldStartTileNotSetIndex) {
  const TheaterData t = Parse();
  EXPECT_EQ(t.clear_tile, 0);
  EXPECT_EQ(t.shore_pieces, 21);
  EXPECT_EQ(t.water_set, 63);
  EXPECT_EQ(t.green_tile, 77);
}

// 键指向不存在的组、或键根本没有 -> -1（原版 ReadInteger 默认值 -1，
// 枚举时没有任何组能命中 -1，全局就保持不变/未设）。
TEST(Theater, MissingOrDanglingKeyGivesMinusOne) {
  const TheaterData t = Parse();
  EXPECT_EQ(t.rough_tile, -1);      // General 里压根没这个键
  EXPECT_EQ(t.ramp_base, -1);
}

// 交叉验证：exe 里硬编码的两个区间宽度，正好等于对应组的 TilesInSet。
// 这是"起始砖号"这条推断能立住的关键证据。
TEST(Theater, HardcodedWidthsMatchTilesInSet) {
  const TheaterData t = Parse();
  EXPECT_EQ(t.TilesInSetStartingAt(t.water_set), 0x0E);      // IsWater 0x485060
  EXPECT_EQ(t.TilesInSetStartingAt(t.shore_pieces), 0x2A);   // 岸砖清扫
}

// 注释、空行、行尾注释、键名大小写都要能吃掉。
TEST(Theater, ToleratesCommentsAndCase) {
  TheaterData t;
  ASSERT_TRUE(ParseTheaterIni(
      "[General]\nWATERSET=1\n[TileSet0000]\nTilesInSet=7\n"
      "[TileSet0001]\ntilesinset = 3 ; three\n", &t));
  EXPECT_EQ(t.water_set, 7);
  EXPECT_EQ(t.total_tiles, 10);
}

TEST(Theater, RejectsIniWithoutTileSets) {
  TheaterData t;
  EXPECT_FALSE(ParseTheaterIni("[General]\nWaterSet = 1\n", &t));
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
