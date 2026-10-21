// 死代码复现 —— 0x5A3A60 全图扫描 + Y*side+X 线性索引黄金对拍
//
// 基准由 .qwen/tmp/_emu_deadcode_5a3a60.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_5a3a60_golden.txt（3 种子 × 4 用例 = 12 行）。
// 每例：给定 side 与 cell 坐标序列（CellIterator 打桩），函数扫描每格并写
// work[Y*side+X] 的 +0x20..0x2C 四个 dword（.rdata 常量 [0x7ED7F0..FC]）。
// 对拍：
//   1. work 写入段（idx → (w20,w24,w28,w2C)）== golden；
//   2. 调用后下一次 Random() == golden next（验证零取数点——不消耗 RNG）。
#include "dead_code_5a3a60.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
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
  std::uint32_t seed;
  int side;
  std::vector<std::pair<int, int>> coords;  // (X, Y) 序列
  // .rdata 四常量（golden 直读，替代写死猜法）
  std::uint32_t c_f0 = 0;
  std::uint32_t c_f4 = 0;
  std::uint32_t c_f8 = 0;
  std::uint32_t c_fc = 0;
  // 期望写入：idx → (w20, w24, w28, w2C)
  std::map<int, std::pair<std::uint32_t, std::string>> writes;
  std::uint32_t next;
};

std::vector<Case> LoadGolden() {
  const std::string path = RA2_GOLDEN_DIR "/deadcode_5a3a60_golden.txt";
  std::ifstream f(path);
  EXPECT_TRUE(f.good()) << "cannot open golden: " << path;
  std::vector<Case> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    Case c;
    unsigned int s, n, f0, f4, f8, fc;
    int side;
    char coords[512], writes[2048];
    // seed=%u side=%d coords=%s consts=(%X,%X,%X,%X) => %s next=0x%X
    if (std::sscanf(line.c_str(),
                    "seed=%u side=%d coords=%511s consts=(%X,%X,%X,%X) => %2047s next=0x%X",
                    &s, &side, coords, &f0, &f4, &f8, &fc, writes, &n) != 9) {
      ADD_FAILURE() << "unparseable golden line: " << line;
      continue;
    }
    c.seed = s;
    c.side = side;
    c.c_f0 = f0;
    c.c_f4 = f4;
    c.c_f8 = f8;
    c.c_fc = fc;
    c.next = n;
    // coords：none 或 (X,Y);(X,Y);...
    if (std::string(coords) != "none") {
      std::istringstream cs(coords);
      std::string tok;
      while (std::getline(cs, tok, ';')) {
        int X, Y;
        if (std::sscanf(tok.c_str(), "(%d,%d)", &X, &Y) != 2) {
          ADD_FAILURE() << "unparseable coords token: " << tok;
          continue;
        }
        c.coords.emplace_back(X, Y);
      }
    }
    // writes：none 或 idxN=(w20,w24,w28,w2C);idxM=(...);...
    if (std::string(writes) != "none") {
      std::istringstream ws(writes);
      std::string tok;
      while (std::getline(ws, tok, ';')) {
        int idx;
        unsigned int w20, w24, w28, w2C;
        if (std::sscanf(tok.c_str(), "idx%d=(%X,%X,%X,%X)",
                        &idx, &w20, &w24, &w28, &w2C) != 5) {
          ADD_FAILURE() << "unparseable write token: " << tok;
          continue;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%08X%08X%08X%08X",
                      w20, w24, w28, w2C);
        c.writes[idx] = {w20, buf};
      }
    }
    cases.push_back(c);
  }
  return cases;
}

// CellIterator 打桩：按 coords 顺序返回下一格，耗尽终止（NULL）。
class GoldenWorld : public DeadCode5A3A60World {
 public:
  explicit GoldenWorld(const std::vector<std::pair<int, int>>& coords)
      : coords_(coords) {}
  bool NextCell(int& x, int& y) override {
    if (pos_ >= coords_.size()) return false;  // NULL 终止
    x = coords_[pos_].first;
    y = coords_[pos_].second;
    ++pos_;
    return true;
  }
  const std::vector<std::pair<int, int>>& coords_;
  std::size_t pos_ = 0;
};

TEST(DeadCode5A3A60, Golden) {
  const auto cases = LoadGolden();
  ASSERT_FALSE(cases.empty());
  for (const auto& c : cases) {
    std::vector<DeadCode5A3A60Context::WorkSlot> work(
        static_cast<std::size_t>(c.side) * c.side);
    Randomizer rng(c.seed);
    GoldenWorld world(c.coords);
    DeadCode5A3A60Context ctx;
    ctx.world = &world;
    ctx.side = c.side;
    // .rdata 四常量从 golden 直读（探针灌 [0x7ED7F0..FC] 的实际值）
    ctx.c_f0 = c.c_f0;
    ctx.c_f4 = c.c_f4;
    ctx.c_f8 = c.c_f8;
    ctx.c_fc = c.c_fc;
    ctx.work = work.data();
    ctx.work_size = work.size();
    DeadCode5A3A60(ctx);
    // 对比 work 写入
    std::map<int, std::string> got;
    for (std::size_t i = 0; i < work.size(); ++i) {
      if (work[i].f20 || work[i].f24 || work[i].f28 || work[i].f2C) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%08X%08X%08X%08X",
                      work[i].f20, work[i].f24, work[i].f28, work[i].f2C);
        got[static_cast<int>(i)] = buf;
      }
    }
    EXPECT_EQ(got.size(), c.writes.size())
        << "seed=" << c.seed << " side=" << c.side;
    for (const auto& kv : c.writes) {
      const auto it = got.find(kv.first);
      EXPECT_NE(it, got.end()) << "missing idx=" << kv.first
                               << " seed=" << c.seed << " side=" << c.side;
      if (it != got.end()) {
        EXPECT_EQ(it->second, kv.second.second)
            << "idx=" << kv.first << " seed=" << c.seed << " side=" << c.side;
      }
    }
    // 零取数点：调用后下一次 Random() == golden next
    const std::uint32_t nxt = rng.Random();
    EXPECT_EQ(nxt, c.next) << "next-rng seed=" << c.seed << " side=" << c.side;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2