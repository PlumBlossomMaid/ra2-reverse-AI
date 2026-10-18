// 死代码复现 —— 0x5A89D0 批量放置主循环黄金对拍
//
// 基准由 .qwen/tmp/_golden_5A89D0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_batch_placer_golden.txt。golden 侧被调族
// （0x5A7D00 判定 / 0x5A7E90 处理 / 0x5A7FA0 清理 / 0x5A6C10 表派发 /
// 0x5A99B0 槽构造 / 0x42D470 打包）全是真身执行，只有 0x5A8F00 取数打了桩
// （返回值按用例 pickret 固定）。下面的 FakeBatchPlacerWorld 用同一套桩语义：
//   · ProbeType（0x5A7D00）：fail-verdict 用例表项全拒（恒 false），其余用例
//     表项计数<=0 → 恒通过（true）——与 golden 的 cfg 配置一致
//   · PickPlace（0x5A8F00）：返回用例 pickret；golden PICK 行的 ecx 即方向
//     索引 (ky-4)&7，C++ 侧记录 (mode, dir) 序列逐条对拍
//   · timeGetTime：golden 覆写成 mov eax,0xC0FFEE00; ret，这里返回固定值
//   · operator new/delete：golden 用 bump 分配器恒成功，这里 AllocTemp 恒真
//
// 对拍点只有三样：Randomizer::call_count（主循环只采样 1 次，rng=1 恒等）、
// 0x5A8F00 调用序列（mode+方向索引）、返回值 al=(placed>1)?1:0。
// 12 用例覆盖：探测耗尽（fail-verdict）、count<=1 不入队（zero/one-count）、
// 首取失败即断（two/nine-pick0、two-blocked/levels/veto）、以及两个雪球用例
// （two-pick1: 4076 次调用 / nine-pick1: 2338 次调用，count 单调涨到 0x3FC
// 触发排空）。
#include "batch_placer.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "map_grid.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

struct Case {
  std::string name;
  int w = 0, h = 0;
  unsigned rng_seed = 0;
  int pickret = 0;
  int rng = 0, ret = 0;
  std::map<int, BatchAbeEntry> abe;                 // idx -> 段表项
  std::vector<std::pair<int, int> > picks;          // (mode, dir) 序列
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_batch_placer_golden.txt";
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
      std::string kind, arrow;
      // golden 布局：CASE name w h seed kind pickret -> rng picks ret
      // picks 数字与 PICK 行数一致，由 c.picks.size() 覆盖断言，这里不另存
      int picks = 0;
      is >> c.name >> c.w >> c.h >> c.rng_seed >> kind >> c.pickret
         >> arrow >> c.rng >> picks >> c.ret;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "ABE") {
      int idx = 0, count = 0;
      is >> idx >> count;
      BatchAbeEntry e;
      e.count = count;
      for (int k = 0; k < count; ++k) {
        int kx = 0, ky = 0;
        is >> kx >> ky;
        e.sub.push_back(std::make_pair(kx, ky));
      }
      c.abe[idx] = e;
    } else if (key == "PICK") {
      int mode = 0, ecx = 0, ret = 0;
      is >> mode >> ecx >> ret;
      c.picks.push_back(std::make_pair(mode, ecx));
    }
  }
  return cases;
}

class FakeBatchPlacerWorld : public DeadBatchPlacerWorld {
 public:
  FakeBatchPlacerWorld(Randomizer& rng, const Case& c)
      : rng_(rng), case_(&c), probe_ok_(c.name != "fail-verdict") {}

  Randomizer& Rng() override { return rng_; }

  // [0x7E1530] timeGetTime stub：golden 覆写成固定值
  std::uint32_t TimeGetTime() override { return 0xC0FFEE00u; }

  // 0x5A7D00：表项计数<=0 恒通过；fail-verdict 用例表项全拒
  bool ProbeType(int type_index, const CellCoord& base) override {
    (void)type_index;
    (void)base;
    return probe_ok_;
  }

  // 被调族清理/派发/处理在 golden 是真身，C++ 侧同桩语义（不消耗随机数、
  // 不影响 pick 序列与返回值，只计数留作观察点）
  void CleanType(int type_index, const CellCoord& base) override {
    (void)type_index;
    (void)base;
    ++cleans_;
  }
  void DispatchType(int type_index, const CellCoord& base) override {
    (void)type_index;
    (void)base;
    ++dispatches_;
  }
  void HandleType(int type_index, const CellCoord& base) override {
    (void)type_index;
    (void)base;
    ++handles_;
  }

  // [0xABE370] 段表：golden 只输出 count>0 的段，查不到返回空段
  const BatchAbeEntry& AbeEntry(int idx) override {
    const std::map<int, BatchAbeEntry>::const_iterator it =
        case_->abe.find(idx);
    if (it != case_->abe.end()) return it->second;
    return empty_;
  }

  // 0x5A8F00 打桩：返回用例 pickret，记录 (mode, dir)
  bool PickPlace(int mode, int dir_index, const CellCoord& target) override {
    (void)target;
    picks.push_back(std::make_pair(mode, dir_index));
    return case_->pickret != 0;
  }

  // operator new/delete：bump 分配器恒成功
  bool AllocTemp() override { return true; }
  void FreeTemp() override {}

  std::vector<std::pair<int, int> > picks;
  int cleans_ = 0;
  int dispatches_ = 0;
  int handles_ = 0;

 private:
  Randomizer& rng_;
  const Case* case_;
  bool probe_ok_;
  BatchAbeEntry empty_;
};

TEST(BatchPlacer, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(12u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    Randomizer rng(c.rng_seed);
    FakeBatchPlacerWorld world(rng, c);
    DeadBatchPlacerContext ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.base = CellCoord{10, 20};   // golden 全用例同坐标

    const int got = DeadCodeBatchPlacer(ctx);

    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count())
        << "取数个数不对（主循环应只采样 1 次）";
    EXPECT_EQ(c.ret, got) << "返回 al=(placed>1)?1:0 不对";
    ASSERT_EQ(c.picks.size(), world.picks.size()) << "0x5A8F00 调用次数不对";
    for (std::size_t i = 0; i < c.picks.size(); ++i) {
      EXPECT_EQ(c.picks[i].first, world.picks[i].first)
          << "第 " << i << " 次调用的 mode 不对";
      EXPECT_EQ(c.picks[i].second, world.picks[i].second)
          << "第 " << i << " 次调用的方向索引 (ky-4)&7 不对";
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
