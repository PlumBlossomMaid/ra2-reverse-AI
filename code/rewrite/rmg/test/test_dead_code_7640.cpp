// 死代码复现 —— 0x5A7640 点集 bounding box 计算器黄金对拍
//
// 基准由 .qwen/tmp/_golden_7640.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_7640_golden.txt。原版侧输入对象
// （+4 = 元素数组 dword、+0x10 = 个数 n）直接由 golden 脚本构造，输出
// 矩形 4×int32 {minX, minY, w, h} 由 emu 读取。下面的
// FakeDeadCode7640World 用同一输入语义（Element(i) 返回 dword：低 16
// X、高 16 Y）并对拍：返回的 out[4] 必须与 golden CASE 行完全一致。
//
// ★解析注意：golden 的 CASE 行是 Python rd32 的**无符号**打印
// （-998 → 4294966298），所以测试先按 uint32_t 解析再转 int32_t——
// 直接 is >> int32_t 会让超出 INT32_MAX 的值被置成 INT_MAX（libstdc++
// C++11 行为），2026-10-19 batch 936 实测踩中。
//
// 对拍点四样：minX/minY/w(=maxX-minX+1)/h(=maxY-minY+1)。8 用例覆盖：
//   empty（n==0 → 哨兵 {999,999,-998,-998}）、single（{5,7,1,1}）、
//   multi（{0,1,9,9}）、neg（负坐标 {-3,-2,8,8}）、allneg（全负 → max
//   保持初值 0：{-2,-2,3,3}）、wide（16 位界 -32768..32767 →
//   {-32768,-32768,65536,65536}）、ff（0xFFFF 字节 = -1：{-1,-1,2,2}）、
//   mixed（{-5,-7,9,14}）。
//
// ★差异提醒（vs batch 934/935 的谓词族）：7640 无 GetCellAt / 谓词 /
// 菱形域——纯数组扫描求 16 位 min/max。无失败路径（返回 void）。
#include "dead_code_7640.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// golden CASE 行是 Python rd32 无符号打印（-998 → 4294966298），先按
// uint32_t 解析再转 int32_t，避免 libstdc++ 对超 INT32_MAX 的输入置
// INT_MAX 的坑。
static std::int32_t ParseI32(std::istringstream& is) {
  std::uint32_t v = 0;
  is >> v;
  return static_cast<std::int32_t>(v);
}

struct Case {
  std::string name;
  int n = 0;
  std::int32_t min_x = 0, min_y = 0, w = 0, h = 0;
  std::vector<std::pair<std::int16_t, std::int16_t> > pts;  // PT 行
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_7640_golden.txt";
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
      std::string arrow;
      is >> c.name >> c.n >> arrow;
      c.min_x = ParseI32(is);
      c.min_y = ParseI32(is);
      c.w = ParseI32(is);
      c.h = ParseI32(is);
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "PT") {
      int x = 0, y = 0;
      is >> x >> y;
      c.pts.push_back(std::make_pair(static_cast<std::int16_t>(x),
                                     static_cast<std::int16_t>(y)));
    }
  }
  return cases;
}

class FakeDeadCode7640World : public DeadCode7640World {
 public:
  explicit FakeDeadCode7640World(const Case& c) : c_(c) {}
  std::uint32_t Element(std::int32_t i) override {
    const std::uint32_t x =
        static_cast<std::uint16_t>(c_.pts[i].first);   // 低 16 = X
    const std::uint32_t y =
        static_cast<std::uint16_t>(c_.pts[i].second);  // 高 16 = Y
    return (y << 16) | x;
  }

 private:
  const Case& c_;
};

TEST(DeadCode7640, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(8u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    FakeDeadCode7640World world(c);
    DeadCode7640Context ctx;
    ctx.world = &world;
    ctx.count = c.n;

    std::int32_t out[4] = {0, 0, 0, 0};
    DeadCode7640(ctx, out);

    EXPECT_EQ(c.min_x, out[0]) << "minX 不对";
    EXPECT_EQ(c.min_y, out[1]) << "minY 不对";
    EXPECT_EQ(c.w, out[2]) << "w 不对";
    EXPECT_EQ(c.h, out[3]) << "h 不对";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2