// 阶段 9 高度场（0x5A2F50）的黄金对拍。
//
// 基准由 code/randmap/dump_height_field_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/height_field_golden.txt。每个用例给一张空工作数组
// （只填菱形内槽位的打包坐标）与若干锚点，跑完逐格比 height 与 slope 的
// **64 位字节**——不是近似比较，因为整条链上没有任何一步允许误差。
//
// 用例专门覆盖：Ruggedness = 0 / 9 / 负数（三条「整段跳过」出口）、恰好 10
// 的边界、锚点把 slope 重置成 0.0025 的路径，以及大 Ruggedness 下 height
// 撞上 ±2 截断的情形。
#include "height_field.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// 基准里存的是 struct.pack("<d", v).hex()：8 个字节按**小端**逐个写成两位十六进制
double HexToDouble(const std::string& s) {
  std::uint64_t bits = 0;
  for (int b = 0; b < 8; ++b) {
    unsigned byte = 0;
    for (int k = 0; k < 2; ++k) {
      const char c = s[static_cast<std::size_t>(b * 2 + k)];
      const unsigned v = (c >= '0' && c <= '9')
                             ? static_cast<unsigned>(c - '0')
                             : static_cast<unsigned>((c | 32) - 'a' + 10);
      byte = (byte << 4) | v;
    }
    bits |= static_cast<std::uint64_t>(byte) << (8 * b);
  }
  double d;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

std::vector<double> ParseList(const std::string& s) {
  std::vector<double> out;
  std::string cur;
  for (std::size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || s[i] == ',') {
      if (!cur.empty()) out.push_back(HexToDouble(cur));
      cur.clear();
    } else {
      cur += s[i];
    }
  }
  return out;
}

struct HfCase {
  std::string name;
  int w = 0, h = 0, rug = 0;
  unsigned seed = 0;
  std::vector<CellCoord> anchor;
  std::vector<double> anchor_h;
  std::vector<double> height, slope;
};

std::vector<HfCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/height_field_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<HfCase> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "CASE") {
      HfCase c;
      int na = 0;
      is >> c.name >> c.w >> c.h >> c.rug >> c.seed >> na;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    HfCase& c = cases.back();
    if (key == "ANCHOR") {
      CellCoord p;
      std::string hx;
      is >> p.x >> p.y >> hx;
      c.anchor.push_back(p);
      c.anchor_h.push_back(HexToDouble(hx));
      continue;
    }
    std::string body;
    is >> body;
    if (key == "HEIGHT") c.height = ParseList(body);
    else if (key == "SLOPE") c.slope = ParseList(body);
  }
  return cases;
}

TEST(HeightField, MatchesOriginal) {
  FpuChopScope fpu;
  const std::vector<HfCase> cases = LoadGolden();
  ASSERT_EQ(9u, cases.size());

  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const HfCase& c = cases[ci];
    SCOPED_TRACE(c.name);
    const MapGrid grid(c.w, c.h);
    const int side = grid.side();
    const int n = side * side;
    ASSERT_EQ(static_cast<std::size_t>(n), c.height.size());

    std::vector<PaintWorkCell> work(static_cast<std::size_t>(n),
                                    PaintWorkCell());
    for (int y = 0; y < side; ++y) {
      for (int x = 0; x < side; ++x) {
        if (!grid.InMap(x, y)) continue;
        PaintWorkCell& w = work[static_cast<std::size_t>(y * side + x)];
        w.coord.x = x;
        w.coord.y = y;
      }
    }
    for (std::size_t i = 0; i < c.anchor.size(); ++i) {
      PaintWorkCell& w = work[static_cast<std::size_t>(
          c.anchor[i].y * side + c.anchor[i].x)];
      w.flag45 = true;
      w.height = c.anchor_h[i];
    }

    Randomizer rng(c.seed);
    NormalDeviate nd(rng);
    GenerateHeightField(grid, work, c.rug, nd);

    for (int i = 0; i < n; ++i) {
      ASSERT_EQ(c.height[i], work[i].height)
          << "第 " << i << " 格（x=" << i % side << " y=" << i / side
          << "）的 height 不对";
      ASSERT_EQ(c.slope[i], work[i].slope)
          << "第 " << i << " 格（x=" << i % side << " y=" << i / side
          << "）的 slope 不对";
    }
  }
}

// 取整是「远离零」，而且恰好 0.0 原样不动
TEST(HeightField, RoundHalfAwayFromZero) {
  FpuChopScope fpu;
  EXPECT_EQ(1.0, RoundHalfAwayFromZero(0.5));
  EXPECT_EQ(-1.0, RoundHalfAwayFromZero(-0.5));
  EXPECT_EQ(2.0, RoundHalfAwayFromZero(1.5));     // ties-to-even 会给 2，这里也是
  EXPECT_EQ(3.0, RoundHalfAwayFromZero(2.5));     // ⚠️ ties-to-even 会给 2
  EXPECT_EQ(-3.0, RoundHalfAwayFromZero(-2.5));
  EXPECT_EQ(0.0, RoundHalfAwayFromZero(0.0));
  EXPECT_EQ(0.0, RoundHalfAwayFromZero(0.49));
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
