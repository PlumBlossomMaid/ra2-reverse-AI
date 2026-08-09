// .sed 参数读取的回归
//
// 样本取自实机生成的 RandMap.Sed（见 docs/random-map/parameters.md）。
#include <gtest/gtest.h>

#include "map_seed_params.h"
#include "map_size.h"
#include "scaled_random.h"

namespace {

// 构造函数 0x595680 写下的默认值
TEST(MapSeedParams, ConstructorDefaultsMatchOriginal) {
  ra2::rmg::MapSeedParams p;
  EXPECT_EQ(p.theater, 0);
  EXPECT_EQ(p.map_type, 1);
  EXPECT_EQ(p.resources, 1);
  EXPECT_EQ(p.ruggedness, 0);
  EXPECT_EQ(p.time, 1);
  EXPECT_EQ(p.water_amount, 0);
  EXPECT_EQ(p.num_players, 2);
  EXPECT_EQ(p.tiberium, 0);
  EXPECT_EQ(p.tiberium_layout, 0);
  EXPECT_EQ(p.vegetation, 0);
  EXPECT_EQ(p.urban_presence, 0);
  EXPECT_EQ(p.width, 0);
  EXPECT_EQ(p.height, 0);
  EXPECT_EQ(p.accessibility, 0);
  EXPECT_EQ(p.region_size, 0);
  EXPECT_EQ(p.seed, -1);
}

constexpr char kSample[] =
    "[RandomMap]\r\n"
    "Description=968f,673a,5730,56fe,\r\n"
    "Width=0\r\nHeight=0\r\nNumPlayers=2\r\nSeed=60556\r\nMapType=1\r\n"
    "Theater=1\r\nTime=3\r\nRegionSize=62\r\nRuggedness=64\r\n"
    "Accessibility=40\r\nWaterAmount=0\r\nTiberium=60\r\n"
    "TiberiumLayout=29\r\nVegetation=78\r\nUrbanPresence=12\r\nResources=3\r\n";

TEST(MapSeedParams, ParsesRealSample) {
  ra2::rmg::MapSeedParams p;
  ASSERT_TRUE(ra2::rmg::ParseSed(kSample, &p));
  EXPECT_EQ(p.seed, 60556);
  EXPECT_EQ(p.theater, 1);
  EXPECT_EQ(p.time, 3);
  EXPECT_EQ(p.region_size, 62);
  EXPECT_EQ(p.ruggedness, 64);
  EXPECT_EQ(p.accessibility, 40);
  EXPECT_EQ(p.tiberium, 60);
  EXPECT_EQ(p.tiberium_layout, 29);
  EXPECT_EQ(p.vegetation, 78);
  EXPECT_EQ(p.urban_presence, 12);
  EXPECT_EQ(p.resources, 3);
  // "随机地图"
  ASSERT_EQ(p.description.size(), 4u);
  EXPECT_EQ(p.description[0], u'\u968f');
  EXPECT_EQ(p.description[3], u'\u56fe');
  EXPECT_EQ(ra2::rmg::EncodeDescription(p.description), "968f,673a,5730,56fe,");
}

// 缺失的键保留当前值——原版把字段现值当 ReadInteger 的 default 传进去
TEST(MapSeedParams, MissingKeyKeepsCurrentValue) {
  ra2::rmg::MapSeedParams p;
  p.tiberium = 77;
  ASSERT_TRUE(ra2::rmg::ParseSed("[RandomMap]\nSeed=5\n", &p));
  EXPECT_EQ(p.tiberium, 77);
  EXPECT_EQ(p.seed, 5);
}

// 西木 Get_Int 的三条取值路径（0x5276D0）
TEST(MapSeedParams, IniGetIntAcceptsHexForms) {
  EXPECT_EQ(ra2::rmg::IniGetInt("$1f", 0), 31);
  EXPECT_EQ(ra2::rmg::IniGetInt("1Fh", 0), 31);
  EXPECT_EQ(ra2::rmg::IniGetInt("31", 0), 31);
  EXPECT_EQ(ra2::rmg::IniGetInt("-1", 0), -1);
  EXPECT_EQ(ra2::rmg::IniGetInt("", 42), 42);
}

// 参数落到尺寸上：这份样本是 2 人图、档位 0/0
TEST(MapSeedParams, SampleFeedsMapSize) {
  ra2::rmg::MapSeedParams p;
  ASSERT_TRUE(ra2::rmg::ParseSed(kSample, &p));
  ra2::rmg::FpuChopScope chop;
  ra2::rmg::MapSize s =
      ra2::rmg::ComputeMapSize(p.width, p.height, p.num_players, p.map_type);
  EXPECT_EQ(s.width, 70);
  EXPECT_EQ(s.height, 70);
  EXPECT_EQ(s.side(), 141);
}

}  // namespace
