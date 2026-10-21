// 死代码复现 —— 0x5A1E10 MapType 3/4 门禁编排黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a1e10.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a1e10_golden.txt（3 种子 × 4 用例 = 12 行）。
// 每例：给定 this->[0x3C]（maptype），门禁命中（==4||==3）时按固定顺序回调
// 五个子调用（0x58EBC0 → 0x58EF10 → 0x5A19E0 → RepaintAll(0,-1) → 0x5A17F0）；
// 不命中时零回调直接返回。
// 对拍：
//   1. 调用序列（名字 + ecx + RepaintAll 实参）== golden calls；
//   2. 调用后下一次 Random() == golden next（验证零取数点——函数不消耗 RNG）。
#include "dead_code_5a1e10.h"

#include <cstdint>
#include <cstdio>
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

struct Case {
  std::uint32_t seed;
  std::uint32_t type;
  std::string calls;   // "none" 或逗号分隔调用序列
  std::uint32_t next;
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a1e10_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, t, n;
    char calls[512];
    // seed=%u type=%u => calls=%s next=0x%X（calls 无空格——none 或逗号序列）
    if (std::sscanf(line.c_str(), "seed=%u type=%u => calls=%511s next=0x%X",
                    &s, &t, calls, &n) != 4) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.type = t;
    c.calls = calls;
    c.next = n;
    cases.push_back(c);
  }
  return cases;
}

// 记录五个回调的调用序列（与探针 hook 输出同格式）：
//   "0x58EBC0@30001000,...,repaint(0,-1)@0087F7E8,..."
// C++ 侧无固定地址——用 0 作 this 哨兵，比对时把地址归一化。
class GoldenWorld : public DeadCode5A1E10World {
 public:
  mutable std::vector<std::string> seq;

  void Sub58EBC0() override { seq.push_back("0x58EBC0"); }
  void Sub58EF10() override { seq.push_back("0x58EF10"); }
  void Sub5A19E0() override { seq.push_back("0x5A19E0"); }
  void RepaintAll(int arg1, int arg2) override {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "repaint(%d,%d)", arg1, arg2);
    seq.push_back(buf);
  }
  void Sub5A17F0() override { seq.push_back("0x5A17F0"); }
};

// golden calls 归一化：去掉 @地址 后缀（探针记录 ecx，C++ 侧无地址）
std::string NormalizeGolden(const std::string& g) {
  if (g == "none") return g;
  std::string out;
  std::istringstream iss(g);
  std::string tok;
  bool first = true;
  while (std::getline(iss, tok, ',')) {
    const std::size_t at = tok.find('@');
    const std::string name = at == std::string::npos ? tok : tok.substr(0, at);
    if (!first) out += ",";
    out += name;
    first = false;
  }
  return out;
}

TEST(DeadCode5A1E10, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    Randomizer rng(c.seed);
    GoldenWorld world;
    DeadCode5A1E10Context ctx;
    ctx.world = &world;
    ctx.maptype = c.type;
    DeadCode5A1E10(ctx);
    std::string got;
    for (std::size_t i = 0; i < world.seq.size(); ++i) {
      if (i) got += ",";
      got += world.seq[i];
    }
    if (got.empty()) got = "none";
    EXPECT_EQ(got, NormalizeGolden(c.calls))
        << "seed=" << c.seed << " type=" << c.type;
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed << " type=" << c.type;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2