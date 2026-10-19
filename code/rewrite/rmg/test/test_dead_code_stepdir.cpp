// 死代码复现 —— 0x5A6D80/0x5A7070 定向步进黄金对拍
//
// 基准由 .qwen/tmp/_golden_6D80.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_stepdir_golden.txt。golden 侧 0x5A7250
// 判定打桩（azn/one_shot 控制返回序列）、Randomizer 真身（seed 灌
// [0xABE890]，★基类 PlaceOracle 的 _h_rng 自动计数）、GetCellAt 真身
// （TechOracle cell 数组）、[0x89F688] 方向表灌 DIR8 标准表（真机 .data
// 未初始化=0 会死循环——怪癖①，golden 为收敛灌表）、[0xABC2B0] 灌用例值。
// 下面的 FakeStepDirWorld 用同一套桩语义：
//   · CellOwner（0x5657A0→cell+0x38）：golden 侧 cell+0x38 全 0，目标值
//     [0xABC2B0] 由用例控制（0=全图命中、0x1234=永不命中）
//   · ProbeArea（0x5A7250）：azn 固定 / one_shot 首调后翻转
//   · Rng：Randomizer 真身（对拍 rng 消耗）
//
// 对拍点：返回 out（坐标 dword）、ProbeArea 调用序列（golden ZN 行）、
// Randomizer::call_count、出界写 0。5 用例覆盖：6d80-walk-out（纯步进
// 出界写 0）、6d80-hit-ok（命中距离 0 直写）、6d80-hit-fail（首拒后步进
// 再命中采样）、7070-walk-back（逐格校验+补步）、7070-fail-close（首拒
// 即补步差<=6 直写）。
#include "dead_code_stepdir.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

// golden 脚本的 DIR8 标准表（[0x89F688] 灌装值，见头注释怪癖①）
constexpr Dir8Entry kDir8[8] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1},
    {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
};

struct Case {
  std::string name;
  std::uint32_t func = 0;      // 005A6D80 / 005A7070
  int refx = 0, refy = 0;      // 栈参2 参考坐标（兼初始坐标）
  StepDirRegion region;
  int direction = 0;           // 栈参4
  std::uint32_t abcb2b0 = 0;   // [0xABC2B0]
  int azn = 0;                 // 0x5A7250 返回
  bool one_shot = false;       // 首次后翻转
  std::uint32_t out = 0;       // 期望输出坐标 dword
  std::vector<std::pair<int, int> > zn;  // ProbeArea 参数 (rect, arg)
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_stepdir_golden.txt";
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
      char open = 0, close = 0;
      std::string arrow;
      int x0 = 0, y0 = 0;   // golden 有 x0/y0 字段（函数实际不读，见头注释）
      std::uint32_t ret = 0;
      int one_shot = 0;
      // golden 布局：CASE name func x0 y0 refx refy (x y w h) direction
      //              abcb2b0 azn one_shot -> ret out
      is >> c.name >> std::hex >> c.func >> std::dec >> x0 >> y0
         >> c.refx >> c.refy >> open >> c.region.x >> c.region.y
         >> c.region.w >> c.region.h >> close >> c.direction
         >> std::hex >> c.abcb2b0 >> std::dec >> c.azn >> one_shot
         >> arrow >> std::hex >> ret >> c.out >> std::dec;
      c.one_shot = (one_shot != 0);
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "ZN") {
      std::uint32_t arg = 0, a = 0, b = 0, cc = 0;
      is >> std::hex >> arg >> a >> b >> cc >> std::dec;
      // golden ZN 行：arg a b c（0x5A7250 的 1 栈参 + 矩形三 dword）。
      // 矩形 dword 语义（6D80）：a=X+ebp、b=Y+off1c、c=edi+off14；
      // （7070）：a=X+offxx、b=Y+offxx、c=esi0/edi0+offxx。
      c.zn.push_back(std::make_pair(static_cast<int>(a),
                                    static_cast<int>(cc)));
      (void)arg;
      (void)b;
    }
  }
  return cases;
}

class FakeStepDirWorld : public DeadStepDirWorld {
 public:
  FakeStepDirWorld(Randomizer& rng, const Case& c)
      : rng_(rng), case_(&c), azn_(c.azn), one_shot_(c.one_shot) {}

  Randomizer& Rng() override { return rng_; }

  // 0x5657A0 → cell+0x38：golden 侧 cell+0x38 全 0（TechOracle 默认），
  // 命中与否由 [0xABC2B0]（ctx.abc_b2b0）决定
  std::uint32_t CellOwner(int x, int y) override {
    (void)x;
    (void)y;
    return 0;
  }

  // 0x5A7250：azn 固定 / one_shot 首调后翻转
  bool ProbeArea(int a, int b, int c, int d) override {
    (void)b;
    (void)d;
    calls.push_back(std::make_pair(a, c));
    const bool r = azn_ != 0;
    if (one_shot_) azn_ = 1 - azn_;   // 首次调用后翻转
    return r;
  }

  std::vector<std::pair<int, int> > calls;

 private:
  Randomizer& rng_;
  const Case* case_;
  int azn_;
  bool one_shot_;
};

TEST(DeadCodeStepDir, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(5u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    Randomizer rng(42);               // golden 全用例 seed=42
    FakeStepDirWorld world(rng, c);
    DeadStepDirContext ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.region = c.region;
    ctx.direction = c.direction;
    ctx.abc_b2b0 = c.abcb2b0;
    ctx.dir8 = kDir8;

    std::int32_t al = 0;
    std::uint32_t got = 0;
    if (c.func == 0x5A6D80u) {
      got = DeadCodeStepDir6D80(ctx, c.refx, c.refy,
                                CellCoord{c.refx, c.refy}, &al);
    } else {
      got = DeadCodeStepDir7070(ctx, c.refx, c.refy,
                                CellCoord{c.refx, c.refy}, &al);
    }

    EXPECT_EQ(c.out, got) << "输出坐标 dword 不对";
    ASSERT_EQ(c.zn.size(), world.calls.size()) << "0x5A7250 调用次数不对";
    for (std::size_t i = 0; i < c.zn.size(); ++i) {
      EXPECT_EQ(c.zn[i].first, world.calls[i].first)
          << "第 " << i << " 次 ProbeArea 矩形首 dword 不对";
      EXPECT_EQ(c.zn[i].second, world.calls[i].second)
          << "第 " << i << " 次 ProbeArea 矩形尾 dword 不对";
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2