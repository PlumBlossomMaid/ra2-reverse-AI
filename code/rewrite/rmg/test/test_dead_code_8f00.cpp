// 死代码复现 —— 0x5A8F00 正态抖动取数黄金对拍
//
// 基准由 .qwen/tmp/_golden_8F00.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_8f00_golden.txt。golden 侧 NormalDeviate
// （0x5980C0，fyl2x 在 Unicorn 不可信）打桩按 ndseq 序列注入，0x5A7D00
// 判定真身执行（但表族对象 +0x2E8/+0x2E4 越界写被清零 → 计数=0 恒返 1）。
// 下面的 FakeDeadCode8F00World 用同一套桩语义：
//   · NormalDeviate：返回用例 ndseq 序列，耗尽返 1.0
//   · ProbeFamily（0x5A7D00）：恒 true（与 golden 观测一致，怪癖②）
//   · AbeEntry（[0xABE370] 12B 项）与双表（T12 行）按 golden 配置行重建
//
// 对拍点四样：返回值 al、NormalDeviate 调用次数、Randomizer::call_count、
// out2/out3/out4 三个输出指针的写入值。8 用例覆盖：jump-easy（mode<=1
// 捷径）、norm-ok（正态段一次命中）、norm-reject（v>mode 拒绝一次重采）、
// n2-sampling（n==2 特殊采样，条目数 16 规避无符号读死锁）、lookup-miss
// （子表域不匹配 → esi=count 越界读=0 → 坐标差=B）、count-zero（count=0
// 跳过于表扫描）、verdict-fail（表族拒绝路径不可达，恒通过）、
// domain7-hit（A=7 命中域 7 子表项）。
#include "dead_code_8f00.h"

#include <cstdlib>
#include <fstream>
#include <map>
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

struct Case {
  std::string name;
  int mode = 0;
  int A = 0;
  int bx = 0, by = 0;
  int tid = 0;
  std::vector<double> ndseq;
  int ret = 0, nd_calls = 0, rng = 0;
  std::uint32_t out2 = 0, out3 = 0, out4 = 0;
  std::map<int, Abe8F00Entry> abe;                 // [0xABE370] 项
  std::map<int, DualTableRow> table;               // T12 双表
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_8f00_golden.txt";
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
      std::string ndseq_tok;
      is >> c.name >> c.mode >> c.A >> c.bx >> c.by >> c.tid
         >> ndseq_tok >> arrow >> c.ret >> c.nd_calls >> c.rng
         >> std::hex >> c.out2 >> c.out3 >> c.out4 >> std::dec;
      std::istringstream ns(ndseq_tok);
      std::string item;
      while (std::getline(ns, item, ',')) {
        c.ndseq.push_back(std::strtod(item.c_str(), nullptr));
      }
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "T12") {
      int idx = 0, count = 0;
      is >> idx >> count;
      DualTableRow row;
      row.ebp = count;
      for (int k = 0; k < count; ++k) {
        int id = 0;
        is >> id;
        row.ids.push_back(id);
      }
      c.table[idx] = row;
    } else if (key == "ABE") {
      int idx = 0, count = 0, tid = 0;
      is >> idx >> count >> tid;
      Abe8F00Entry e;
      e.count = count;
      e.family_id = tid;
      // ★子表项数不一定等于 count：count-zero 用例 count=0 但子表仍有
      //   1 项（0x5A90DA 用 esi=0 读子表[0]，golden 进料必须给有效子表
      //   指针否则崩）。读剩余全部 (coord, domain) 对。
      std::uint32_t coord = 0;
      int domain = 0;
      while (is >> std::hex >> coord >> std::dec >> domain) {
        e.sub.push_back(std::make_pair(coord, domain));
      }
      c.abe[idx] = e;
    }
  }
  return cases;
}

class FakeDeadCode8F00World : public DeadCode8F00World {
 public:
  FakeDeadCode8F00World(Randomizer& rng, const Case& c)
      : rng_(rng), case_(&c), ndseq_(c.ndseq), nd_pos_(0), nd_calls_(0) {}

  Randomizer& Rng() override { return rng_; }

  double NormalDeviate() override {
    ++nd_calls_;
    if (nd_pos_ < ndseq_.size()) {
      return ndseq_[nd_pos_++];
    }
    return 1.0;   // golden 桩耗尽返回 1.0
  }

  const Abe8F00Entry& AbeEntry(int idx) override {
    const std::map<int, Abe8F00Entry>::const_iterator it =
        case_->abe.find(idx);
    if (it != case_->abe.end()) return it->second;
    return empty_;
  }

  // 0x5A7D00：怪癖②计数<=0 恒返 1（golden 全部用例观测一致）
  bool ProbeFamily(int family_id) override {
    (void)family_id;
    return true;
  }

  int nd_calls() const { return nd_calls_; }

 private:
  Randomizer& rng_;
  const Case* case_;
  std::vector<double> ndseq_;
  std::size_t nd_pos_ = 0;
  int nd_calls_ = 0;
  Abe8F00Entry empty_;
};

TEST(DeadCode8F00, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(8u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    Randomizer rng(42);                 // golden 全用例 seed=42
    FakeDeadCode8F00World world(rng, c);
    DeadCode8F00Context ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.base = CellCoord{c.bx, c.by};
    ctx.A = c.A;
    ctx.mode = c.mode;
    ctx.table = c.table;

    std::uint32_t out2 = 0xDEADBEEF, out3 = 0xDEADBEEF, out4 = 0xDEADBEEF;
    const int got = DeadCode8F00(ctx, &out2, &out3, &out4);

    EXPECT_EQ(c.ret, got) << "返回 al 不对";
    EXPECT_EQ(c.nd_calls, world.nd_calls()) << "NormalDeviate 调用次数不对";
    EXPECT_EQ(static_cast<long long>(c.rng), rng.call_count())
        << "Randomizer 取数个数不对";
    EXPECT_EQ(c.out2, out2) << "out2（表族 id）写入不对";
    EXPECT_EQ(c.out3, out3) << "out3（坐标差 dword）写入不对";
    EXPECT_EQ(c.out4, out4) << "out4（命中表项索引）写入不对";
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2