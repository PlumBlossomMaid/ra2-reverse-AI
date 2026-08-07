// 阶段 6 的对拍。
//
// 主体是 ChooseWaypoints（0x594F40）的黄金对拍：基准由
// code/randmap/dump_start_points_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/start_points_golden.txt。270 例覆盖
//   候选数 0/1/2/3/7/20 x startCount 0/1/3 x (scale, players) 三组 x 五种点型
// 正好把那几处怪癖全踩到：want 的两次兜底改写、startCount == 0 时的
// `return NULL`、只有一个候选时的提前返回、以及**同一个格子被选两次**。
//
// 另外两个是纯算术件：名额分配（累计取整）与排序键。
#include "start_points.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see rmg/CMakeLists.txt)"
#endif

const int kSide = 160;

struct WpCase {
  std::string kind;
  int n = 0, start_count = 0, scale = 0, players = 2;
  std::vector<CellCoord> cand;
  std::vector<int> owner;
  bool null_result = false;
  std::vector<CellCoord> out;
};

std::vector<std::string> Split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == sep) { out.push_back(cur); cur.clear(); }
    else cur += s[i];
  }
  out.push_back(cur);
  return out;
}

std::vector<WpCase> LoadGolden() {
  const std::string path =
      std::string(RA2_GOLDEN_DIR) + "/start_points_golden.txt";
  std::ifstream f(path.c_str());
  EXPECT_TRUE(f.good()) << "打不开基准档 " << path;
  std::vector<WpCase> cases;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream is(line);
    std::string key;
    is >> key;
    if (key == "CASE") {
      WpCase c;
      is >> c.kind >> c.n >> c.start_count >> c.scale >> c.players;
      cases.push_back(c);
      continue;
    }
    if (cases.empty()) continue;
    WpCase& c = cases.back();
    if (key == "NULL") { c.null_result = true; continue; }
    std::string body;
    is >> body;
    if (body == "-") continue;
    const std::vector<std::string> items = Split(body, ',');
    for (std::size_t i = 0; i < items.size(); ++i) {
      const std::vector<std::string> f3 = Split(items[i], ':');
      CellCoord p;
      p.x = std::atoi(f3[0].c_str());
      p.y = std::atoi(f3[1].c_str());
      if (key == "CAND") {
        c.cand.push_back(p);
        c.owner.push_back(std::atoi(f3[2].c_str()));
      } else {
        c.out.push_back(p);
      }
    }
  }
  return cases;
}

TEST(StartPoints, ChooseWaypointsMatchesOriginal) {
  FpuChopScope fpu;
  const std::vector<WpCase> cases = LoadGolden();
  ASSERT_EQ(270u, cases.size());

  for (std::size_t ci = 0; ci < cases.size(); ++ci) {
    const WpCase& c = cases[ci];
    std::ostringstream tag;
    tag << c.kind << " n=" << c.n << " sc=" << c.start_count
        << " scale=" << c.scale << " players=" << c.players;
    SCOPED_TRACE(tag.str());
    ASSERT_EQ(static_cast<std::size_t>(c.n), c.cand.size());

    // 工作数组只有 +0x38 被读到；基准那边也是把区域编号直接铺进去的
    std::vector<PaintWorkCell> work(
        static_cast<std::size_t>(kSide) * kSide, PaintWorkCell());
    for (std::size_t i = 0; i < c.cand.size(); ++i) {
      work[static_cast<std::size_t>(c.cand[i].y * kSide + c.cand[i].x)]
          .blob_id = c.owner[i];
    }

    StartPointContext ctx;
    ctx.side = kSide;
    ctx.work = &work;
    ctx.num_players = c.players;
    ctx.waypoint_scale = c.scale;

    bool ok = false;
    const std::vector<CellCoord> got =
        ChooseWaypoints(ctx, c.cand, c.start_count, &ok);
    if (c.null_result) {
      EXPECT_FALSE(ok);
      continue;
    }
    ASSERT_TRUE(ok);
    ASSERT_EQ(c.out.size(), got.size()) << "选出的点数不对";
    for (std::size_t i = 0; i < got.size(); ++i) {
      EXPECT_EQ(c.out[i].x, got[i].x) << "第 " << i << " 个点的 X";
      EXPECT_EQ(c.out[i].y, got[i].y) << "第 " << i << " 个点的 Y";
    }
  }
}

// 名额分配：累计取整，最后一个吃余数。总数一定分光。
TEST(StartPoints, QuotaSumsToPlayers) {
  FpuChopScope fpu;
  const int shapes[][4] = {
      {1000, 900, 800, 700}, {5000, 400, 400, 400}, {401, 400, 0, 0},
      {12345, 6789, 111, 1}, {400, 400, 400, 400},
  };
  for (int s = 0; s < 5; ++s) {
    for (int n = 1; n <= 4; ++n) {
      std::vector<int> counts;
      for (int i = 0; i < n; ++i) counts.push_back(shapes[s][i]);
      for (int players = 2; players <= 8; ++players) {
        const std::vector<int> q = AllocateStartQuota(counts, players);
        int sum = 0;
        for (int i = 0; i < n; ++i) sum += q[i];
        EXPECT_EQ(players, sum) << "s=" << s << " n=" << n
                                << " players=" << players;
      }
    }
  }
}

// 三个 400 格的区域 + 2 个玩家：累计取整给的是 {1, 0, 1}，不是 {1, 1, 0}。
// 逐个四舍五入会得到 {1, 1, 0} 并超发，这正是原版用累计法的原因。
TEST(StartPoints, QuotaAccumulateRounding) {
  FpuChopScope fpu;
  std::vector<int> counts;
  counts.push_back(400);
  counts.push_back(400);
  counts.push_back(400);
  const std::vector<int> q = AllocateStartQuota(counts, 2);
  EXPECT_EQ(1, q[0]);
  EXPECT_EQ(0, q[1]);
  EXPECT_EQ(1, q[2]);
}

// 排序键：格子多的排前面；格子一样多时按编号升序
TEST(StartPoints, SortKeyOrdering) {
  const int rc = 7;
  EXPECT_LT(StartRegionSortKey(5000, 3, rc), StartRegionSortKey(4000, 0, rc));
  EXPECT_LT(StartRegionSortKey(4000, 1, rc), StartRegionSortKey(4000, 2, rc));
  // regionCount 只是把编号塞进低位的进制，不改变「大区在前」
  EXPECT_LT(StartRegionSortKey(4001, rc - 1, rc), StartRegionSortKey(4000, 0, rc));
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
