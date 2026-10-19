// 死代码复现 —— 0x5A6C10 [0xA8ED2C] 表查询放置黄金对拍
//
// 基准由 .qwen/tmp/_golden_6C10.py 用 Unicorn 指令级模拟原版机器码取得，
// 存档 memory/data/rmg/deadcode_6c10_golden.txt。golden 侧 GetCellAt
// (0x5657A0) 跑真身（hook 只计数），表项虚表槽 0x9C 打桩（逐表项独立桩
// 返回各自数据表基址），RNG 不参与。
// 下面的 FakeDeadCode6C10World 复刻 golden 同一语义并计数：
//   · GetCellAt（0x5657A0）：返回哑元 cell 句柄，记录坐标（对拍 GC 行）
//   · WriteCell：记录 {c38, c11a, c11b, c11c}（对拍 CELL 行）
//   · WriteWork：记录 {x, y, v}（对拍 WORK 行——golden 只记录非 0 值）
//   · WorkPresent：[0xABED10] 是否可用（work-zero 用例为 false）
//
// 对拍点四样：GetCellAt 次数 + 坐标序列 + CELL 字段写入序列 + WORK 写入
// 序列。10 用例覆盖：
//   all-pass（全过，c11a 序列 0..5 实锤索引串=外层×内层+内层）、
//   outer0/inner0（计数 <=0 提前返回）、slot9c-zero（槽 0x9C 返回 0）、
//   corner-fail（源坐标 X+Y<=W 全 fail）、partial-fail（16×2=32 格中
//   21 格过——X-Y>=W 判据在 X=18(j=0)/X=19(j=1) 即拦截，gc=21 黄金真
//   值）、data-zero（索引串 1 数据项 0 → 跳过写 cell/work 但 GetCellAt
//   照调，gc=6 cell=5 work=5）、layer-m1（层高 -1 → cell+0x11B 不写）、
//   work-zero（[0xABED10]=0 → work 写入整个跳过）、index-1（表索引 1
//   查第二表项，c294=0x5678 生效）。
#include "dead_code_6c10.h"

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

struct Case {
  std::string name;
  int index = 0, sx = 0, sy = 0, inner = 0, outer = 0;
  std::uint32_t c294 = 0;
  int layer = 0, workv = 0;
  int gc = 0;
  std::vector<std::pair<int, int> > gc_seq;                  // GC 行
  std::vector<std::vector<std::uint32_t> > cells;            // CELL x y c38 c11a c11b c11c
  std::vector<std::vector<std::uint32_t> > works;            // WORK x y v
};

std::vector<Case> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/deadcode_6c10_golden.txt";
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
      std::uint32_t c294 = 0;
      is >> c.name >> c.index >> c.sx >> c.sy >> c.inner >> c.outer
         >> std::hex >> c294 >> std::dec >> c.layer >> c.workv
         >> arrow >> c.gc;
      c.c294 = c294;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    Case& c = cases.back();
    if (key == "GC") {
      int gx = 0, gy = 0;
      is >> gx >> gy;
      c.gc_seq.push_back(std::make_pair(gx, gy));
    } else if (key == "CELL") {
      std::vector<std::uint32_t> row(6, 0);
      for (int i = 0; i < 6; ++i) is >> row[i];
      c.cells.push_back(row);
    } else if (key == "WORK") {
      std::vector<std::uint32_t> row(3, 0);
      for (int i = 0; i < 3; ++i) is >> row[i];
      c.works.push_back(row);
    }
  }
  return cases;
}

class FakeDeadCode6C10World : public DeadCode6C10World {
 public:
  explicit FakeDeadCode6C10World(bool work_present) : work_(work_present) {}

  void* GetCellAt(int x, int y) override {
    ++gc_;
    gc_seq_.push_back(std::make_pair(x, y));
    return reinterpret_cast<void*>(0x5A000000 + gc_);
  }
  void WriteCell(void*, std::uint32_t c38, std::uint8_t c11a,
                 std::uint8_t c11b, std::uint8_t c11c) override {
    std::vector<std::uint32_t> row(6, 0);
    row[0] = static_cast<std::uint32_t>(gc_seq_.back().first);
    row[1] = static_cast<std::uint32_t>(gc_seq_.back().second);
    row[2] = c38;
    row[3] = c11a;
    row[4] = c11b;
    row[5] = c11c;
    cells_.push_back(row);
  }
  void WriteWork(int x, int y, std::int32_t workv) override {
    if (workv == 0) return;   // golden 只记录非 0 值
    std::vector<std::uint32_t> row(3, 0);
    row[0] = static_cast<std::uint32_t>(x);
    row[1] = static_cast<std::uint32_t>(y);
    row[2] = static_cast<std::uint32_t>(workv);
    works_.push_back(row);
  }
  bool WorkPresent() override { return work_; }

  int gc_ = 0;
  std::vector<std::pair<int, int> > gc_seq_;
  std::vector<std::vector<std::uint32_t> > cells_;
  std::vector<std::vector<std::uint32_t> > works_;

 private:
  bool work_ = true;
};

void BuildTable(const Case& c, std::vector<DeadCode6C10TableItem>* table) {
  // index-1 用例是双表项（第 0 项哑元不会被访问，黄金脚本原样构造）
  if (c.index == 1) {
    DeadCode6C10TableItem d0;
    d0.slot9c_ok = false;              // 哑元（不进主函数）
    table->push_back(d0);
    DeadCode6C10TableItem d1;
    d1.inner_count = c.inner;
    d1.outer_count = c.outer;
    d1.c294 = c.c294;
    d1.slot9c_ok = c.name != "slot9c-zero";
    for (int i = 0; i < c.inner * c.outer; ++i) {
      DeadCode6C10DataItem di;
      di.present = true;
      di.b28 = 0x28;
      di.b2a = 0x2A;
      // data-zero：索引串 1（外层0×内层3+内层1 → 格 (9,6)）的项缺失
      if (c.name == "data-zero" && i == 1) di.present = false;
      d1.data.push_back(di);
    }
    table->push_back(d1);
    return;
  }
  DeadCode6C10TableItem item;
  item.inner_count = c.inner;
  item.outer_count = c.outer;
  item.c294 = c.c294;
  item.slot9c_ok = c.name != "slot9c-zero";
  for (int i = 0; i < c.inner * c.outer; ++i) {
    DeadCode6C10DataItem di;
    di.present = true;
    di.b28 = 0x28;
    di.b2a = 0x2A;
    if (c.name == "data-zero" && i == 1) di.present = false;
    item.data.push_back(di);
  }
  table->push_back(item);
}

TEST(DeadCode6C10, MatchesOriginal) {
  const std::vector<Case> cases = LoadGolden();
  ASSERT_EQ(10u, cases.size());
  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const Case& c = cases[ci];
    SCOPED_TRACE(c.name);

    std::vector<DeadCode6C10TableItem> table;
    BuildTable(c, &table);
    const bool work_present = c.name != "work-zero";
    FakeDeadCode6C10World world(work_present);
    DeadCode6C10Context ctx;
    ctx.world = &world;
    ctx.table = &table;
    ctx.index = c.index;
    ctx.src_x = static_cast<std::int16_t>(c.sx);
    ctx.src_y = static_cast<std::int16_t>(c.sy);
    ctx.workv = c.workv;
    ctx.layer = c.layer;
    ctx.w_bound = 12;                 // [0xABED04]（golden 环境 W=12）
    ctx.w2h_bound = 12 + 2 * 8;       // [0xABED08] = W+2H = 28
    ctx.side = 12 + 8 + 1;            // [0x89C2DC]

    DeadCode6C10(ctx);

    EXPECT_EQ(c.gc, world.gc_) << "GetCellAt 调用次数不对";
    ASSERT_EQ(c.gc_seq.size(), world.gc_seq_.size())
        << "GetCellAt 坐标序列长度不对";
    for (std::size_t i = 0; i < c.gc_seq.size(); ++i) {
      EXPECT_EQ(c.gc_seq[i].first, world.gc_seq_[i].first)
          << "第 " << i << " 次 GetCellAt 的 X 不对";
      EXPECT_EQ(c.gc_seq[i].second, world.gc_seq_[i].second)
          << "第 " << i << " 次 GetCellAt 的 Y 不对";
    }
    ASSERT_EQ(c.cells.size(), world.cells_.size())
        << "CELL 写入条数不对（" << c.cells.size() << " vs "
        << world.cells_.size() << "）";
    for (std::size_t i = 0; i < c.cells.size(); ++i) {
      for (int k = 0; k < 6; ++k) {
        EXPECT_EQ(c.cells[i][k], world.cells_[i][k])
            << "第 " << i << " 条 CELL 第 " << k << " 字段不对";
      }
    }
    ASSERT_EQ(c.works.size(), world.works_.size())
        << "WORK 写入条数不对（" << c.works.size() << " vs "
        << world.works_.size() << "）";
    for (std::size_t i = 0; i < c.works.size(); ++i) {
      for (int k = 0; k < 3; ++k) {
        EXPECT_EQ(c.works[i][k], world.works_[i][k])
            << "第 " << i << " 条 WORK 第 " << k << " 字段不对";
      }
    }
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2