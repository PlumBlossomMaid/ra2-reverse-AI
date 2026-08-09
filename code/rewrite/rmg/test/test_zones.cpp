// 寻路分区（0x56C510 + 洪泛 0x56CB90）的黄金对拍
//
// 基准由 code/randmap/dump_zones_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/zones_golden.txt。每个用例给一张合成的
// (地面类别, 层高) 小表，跑完整条流水线之后比四样：
//   1) 区数 ZCNT（= MapClass+0x4C，等于真实区数 + 1）
//   2) 每格的区编号 ZONE（洪泛的涂色顺序全在这里，一格不对就炸）
//   3) 返回值 RET —— 格数最多的那个区的编号
//   4) 13 张「区编号 -> 区类型」表 MZ0..MZ12（CSR 邻接 + 倒序 DFS 的产物）
//
// 用例里 chaos* / rocky / holes 是刻意的随机噪声：只有层高忽高忽低、
// 地面类别忽陆忽水，才踩得到洪泛那几个怪癖——右扫门限是 4 而不是 2、
// run_len 可能为 -1 让外层游标倒退、`[0xABDE8C]` 抑制重复登记。
#include "zones.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

std::vector<int> ParseHex(const std::string& s, int digits) {
  std::vector<int> out;
  for (std::size_t i = 0; i + digits <= s.size(); i += digits) {
    out.push_back(static_cast<int>(std::strtol(s.substr(i, digits).c_str(),
                                               nullptr, 16)));
  }
  return out;
}

struct Case {
  std::string name;
  int w = 0, h = 0, count = 0;
  std::vector<int> land, level, zone;
  int ret = 0, zcnt = 0;
  std::vector<std::vector<int> > mz;
};

std::vector<Case> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/zones_golden.txt";
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
      is >> c.name >> c.w >> c.h >> c.count;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    std::string body;
    is >> body;
    if (key == "LAND") c.land = ParseHex(body, 2);
    else if (key == "LVL") c.level = ParseHex(body, 2);
    else if (key == "ZONE") c.zone = ParseHex(body, 4);
    else if (key == "RET") c.ret = std::atoi(body.c_str());
    else if (key == "ZCNT") c.zcnt = std::atoi(body.c_str());
    else if (key.size() > 2 && key.compare(0, 2, "MZ") == 0) {
      c.mz.push_back(ParseHex(body, 4));
    }
  }
  return cases;
}

TEST(Zones, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);
    ASSERT_EQ(static_cast<int>(c.land.size()), c.count);
    ASSERT_EQ(static_cast<int>(c.level.size()), c.count);
    ASSERT_EQ(13u, c.mz.size());

    ZoneTable table(c.w, c.h);
    ASSERT_EQ(c.count, table.count());
    for (int i = 0; i < c.count; ++i) {
      table.at(i).land = static_cast<std::uint8_t>(c.land[i]);
      table.at(i).level = static_cast<std::uint8_t>(c.level[i]);
    }
    const ZoneResult r = table.Rebuild();

    EXPECT_EQ(c.zcnt, r.zone_count);
    EXPECT_EQ(c.ret, r.largest_zone);
    for (int i = 0; i < c.count; ++i) {
      ASSERT_EQ(c.zone[i], static_cast<int>(table.at(i).zone))
          << "第 " << i << " 格（x=" << i % table.side()
          << " y=" << i / table.side() << "）的区编号不对";
    }
    for (int mz = 0; mz < kMovementZoneCount; ++mz) {
      ASSERT_EQ(static_cast<int>(c.mz[mz].size()), r.zone_count);
      for (int z = 0; z < r.zone_count; ++z) {
        ASSERT_EQ(c.mz[mz][z], static_cast<int>(r.zone_type[mz][z]))
            << "移动区 " << mz << " 的第 " << z << " 项不对";
      }
    }
  }
}

// 0x483C80 只在随机地图这条路上被走到，判据只有三条：出图 / 水 / 沙滩，
// 再就是 `Ground[gt].Foot <= 0.01` 那一刀（比的是 double，不是 float）。
TEST(Zones, RecalcLandTypeBranches) {
  float foot[kGroundTypeCount] = {0};
  foot[kGroundClear] = 1.0f;
  foot[kGroundRock] = 0.0f;
  foot[kGroundRough] = 0.5f;
  foot[kGroundIce] = 0.01f;      // 恰好等于门限：`<=` 成立，判不可通行

  EXPECT_EQ(kLandOffMap, RecalcLandType(false, kGroundClear, foot));
  EXPECT_EQ(kLandWater, RecalcLandType(true, kGroundWater, foot));
  EXPECT_EQ(kLandBeach, RecalcLandType(true, kGroundBeach, foot));
  EXPECT_EQ(kLandClear, RecalcLandType(true, kGroundClear, foot));
  EXPECT_EQ(kLandImpassable, RecalcLandType(true, kGroundRock, foot));
  EXPECT_EQ(kLandClear, RecalcLandType(true, kGroundRough, foot));
  EXPECT_EQ(kLandImpassable, RecalcLandType(true, kGroundIce, foot));
  // 水与沙滩在 Foot 判定**之前**返回，就算 Foot 是 0 也照样是 4 / 3
  foot[kGroundWater] = 0.0f;
  foot[kGroundBeach] = 0.0f;
  EXPECT_EQ(kLandWater, RecalcLandType(true, kGroundWater, foot));
  EXPECT_EQ(kLandBeach, RecalcLandType(true, kGroundBeach, foot));
}

// 阶段 1 传 {2, 5, W, H} 给 0x654490，四条夹取全部原样通过（0x599D57）
TEST(Zones, StageOneLocalRect) {
  for (int w = 70; w <= 120; w += 10) {
    for (int h = 70; h <= 120; h += 10) {
      MapRect want;
      want.x = 2; want.y = 5; want.w = w; want.h = h;
      const MapRect got = ClampLocalRect(want, w + 4, h + 12);
      EXPECT_EQ(2, got.x);
      EXPECT_EQ(5, got.y);
      EXPECT_EQ(w, got.w);
      EXPECT_EQ(h, got.h);
    }
  }
}

// 0x578460：菱形上缩 5 行、下缩 6 行、左右各缩 2 列
TEST(Zones, UsableAreaDiamond) {
  const int w = 70, h = 70;
  const int mw = w + 4;
  MapRect local;
  local.x = 2; local.y = 5; local.w = w; local.h = h;
  for (int x = 0; x < mw + 2 * (h + 12) + 2; ++x) {
    for (int y = 0; y < mw + 2 * (h + 12) + 2; ++y) {
      // (2+W)*2 - (W+4) == W，(W+4) - 2*2 == W —— 左右两侧各正好缩 2 列
      const bool want = (x + y > mw + 10) && (x + y <= mw + 2 * h + 12) &&
                        (x - y < w) && (y - x < w);
      EXPECT_EQ(want, IsWithinUsableArea(mw, local, x, y, 0, 0, true))
          << "x=" << x << " y=" << y;
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
