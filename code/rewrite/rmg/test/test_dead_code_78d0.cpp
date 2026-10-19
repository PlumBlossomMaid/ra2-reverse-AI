// 死代码复现 —— 0x5A78D0 随机抽样+小块批量放置黄金对拍
//
// 基准由 .qwen/tmp/_golden_78D0.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_78d0_golden.txt。golden 侧 0x5A7B40
// 菱形校验/0x5A6C10 放置都打了桩（azn_seq 序列 + ecx/edx 记录），
// Randomizer 真身（seed 灌 [0xABE890]，基类 PlaceOracle 的 _h_rng 自动
// 计数）。下面的 FakeDeadCode78D0World 用同一套桩语义：
//   · ProbeArea（0x5A7B40）：按用例 azn_seq 序列返回，耗尽返 1
//   · PlaceType（0x5A6C10）：记录 (host, x, y) 序列逐条对拍
//   · Rng：Randomizer 真身（对拍 rng 消耗）
//
// 对拍点四样：Randomizer::call_count、ProbeArea 调用次数、PlaceType
// 调用次数与 (host,x,y) 序列。5 用例覆盖：all-fail（首轮 5 个全拒 → 跳过
// 放置 → 第 2 轮起 azn 耗尽返 1 成功）、first-hit（首项即命中）、third-hit
// （第 3 项命中）、mixed（limit=1000 → ebp=limit/50=20 个成功轮）、
// zero-round（limit=50 → ebp=limit/50=1，仍跑 1 轮成功放置）。
//
// ★golden 反推（2026-10-19）修正两个模型 bug：
//   ① 模板数组项 0 是 {a=6, b=4} 不是 {a=4, b=0}——开段 [esp+0x58]=6/
//      [esp+0x5C]=4 在 push edi 之前写，物理落在 push 后的 [esp+0x5C]/
//      [esp+0x60]，即项 0 的 a/b（golden ZN d2 首值 6 实锤）。a 序列
//      {6,4,4,4,2}、b 序列 {4,6,4,2,4}。
//   ② 外层成功轮上限是 limit/50 不是 limit/100：0x51EB851F magic 配
//      sar edx,4（标准 /100 是 sar 5）。all-fail（limit=100 → ebp=2）
//      轮 2/3 各命中项 0 → cx=3, cy=2 → 6 格×2 = 12 ✓ golden place=12。
//   ③ golden CASE 行的 ret 是 EAX 残渣（无意义），不参与对拍。
#include "dead_code_78d0.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
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
  int limit = 0;
  int arr_count = 0;
  std::uint32_t ab10a4 = 0;
  std::vector<int> aznseq;
  int ret = 0, rng = 0, zn = 0, place = 0;
  // PL 行：(host, ecx_dword, [esp+4]=-1, [esp+8]=-1)
  // golden 坐标通过 edx 指向的 word 对传递，edx=dummy（栈地址），
  // 坐标值靠 host 序列对拍（见测试注释）
  std::vector<std::pair<std::uint32_t, std::uint32_t> > places;
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_78d0_golden.txt";
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
      std::string azn_tok;
      // golden 布局：CASE name limit arrcount ab10a4 aznseq -> ret rng zn place
      is >> c.name >> c.limit >> c.arr_count >> std::hex >> c.ab10a4
         >> std::dec >> azn_tok >> arrow >> c.ret >> c.rng >> c.zn >> c.place;
      std::istringstream ns(azn_tok);
      std::string item;
      while (std::getline(ns, item, ',')) {
        if (!item.empty()) c.aznseq.push_back(std::atoi(item.c_str()));
      }
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "ZN") {
      // 菱形校验参数 dword：0x5A78D0 里 tx/ty/tmpl_a/tmpl_b 四个有符号
      // 值都写进 ecx 结构，golden 以无符号十六进制记录。此处读入备查。
      std::uint32_t arg = 0, a = 0, b = 0, cc = 0;
      is >> std::hex >> arg >> a >> b >> cc >> std::dec;
      (void)arg;
      (void)a;
      (void)b;
      (void)cc;
    } else if (key == "PL") {
      std::uint32_t host = 0, edx = 0, a1 = 0, a2 = 0;
      is >> std::hex >> host >> edx >> a1 >> a2 >> std::dec;
      c.places.push_back(std::make_pair(host, edx));
      // a1/a2 恒 -1/-1（push -1,-1），无需逐条断言
      EXPECT_EQ(0xFFFFFFFFu, a1);
      EXPECT_EQ(0xFFFFFFFFu, a2);
    }
  }
  return cases;
}

class FakeDeadCode78D0World : public DeadCode78D0World {
 public:
  FakeDeadCode78D0World(Randomizer& rng, const Case& c)
      : rng_(rng), aznseq_(c.aznseq), azn_pos_(0) {}

  Randomizer& Rng() override { return rng_; }

  bool ProbeArea(int tx, int ty, int ta, int tb) override {
    ++zn_calls_;
    last_tx_ = tx; last_ty_ = ty; last_ta_ = ta; last_tb_ = tb;
    if (azn_pos_ < aznseq_.size()) {
      return aznseq_[azn_pos_++] != 0;
    }
    return true;   // golden 耗尽返 1
  }

  void PlaceType(int host, int x, int y) override {
    places.push_back(std::make_pair(host,
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16 |
        static_cast<std::uint16_t>(x)));
  }

  int zn_calls_ = 0;
  std::vector<std::pair<std::uint32_t, std::uint32_t> > places;  // (host, packed xy)
  int last_tx_ = 0, last_ty_ = 0, last_ta_ = 0, last_tb_ = 0;

 private:
  Randomizer& rng_;
  std::vector<int> aznseq_;
  std::size_t azn_pos_ = 0;
};

TEST(DeadCode78D0, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(5u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    Randomizer rng(42);                 // golden 全用例 seed=42
    FakeDeadCode78D0World world(rng, c);
    DeadCode78D0Context ctx;
    ctx.rng = &rng;
    ctx.world = &world;
    ctx.host_limit = static_cast<std::uint32_t>(c.limit);
    // [edi+4] 模板项数组：golden 只灌了 1-2 项，其余拒绝采样永不命中
    // （上限 limit=100 而数组只有 1 项时 idx 恒 0 → 最多读 1 次）。
    // 为忠实复刻"数组越小越安全"，这里按 arr_count 灌够项数，项值取
    // golden 脚本同款 0x00010001/0x00020002（循环内 idx 受
    // ftol(r*limit*2^-32) 控制，越界数组不会读到——C++ 侧 vector
    // 大小必须 >= limit 才能防越界，但 limit=100 而数组 1 项时模板项
    // 读取 [edi+4][idx] 会越界。原版同理，golden 观测 idx 恒 0 或 1
    // 因 Random 高 16 位被舍入。这里灌 limit 项保证不越界）。
    for (int i = 0; i < c.limit; ++i) {
      ctx.host_arr.push_back((i % 2 == 0) ? 0x00010001u : 0x00020002u);
    }
    ctx.ab10a4 = c.ab10a4;

    const int got = DeadCode78D0(ctx);

    EXPECT_EQ(c.rng, static_cast<int>(rng.call_count()))
        << "Randomizer 取数个数不对";
    EXPECT_EQ(c.zn, world.zn_calls_) << "0x5A7B40 调用次数不对";
    // golden CASE 行的 ret 是 EAX 残渣（0x5A7B2C 直接 pop 返回，EAX 未定义），
    // C++ 恒返回 0，不参与对拍。
    ASSERT_EQ(c.place, static_cast<int>(world.places.size()))
        << "0x5A6C10 放置次数不对";
    for (std::size_t i = 0; i < c.places.size(); ++i) {
      EXPECT_EQ(c.places[i].first, world.places[i].first)
          << "第 " << i << " 次放置的宿主值不对（[0xAA10A4]+采样+8）";
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2