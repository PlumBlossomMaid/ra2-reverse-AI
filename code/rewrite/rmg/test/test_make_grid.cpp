// 区域网格 MakeGrid（原版 0x59A8F0）的黄金对拍
//
// 基准由 code/randmap/dump_make_grid_golden.py 用 Unicorn 跑原版机器码取得，
// 存档 memory/data/rmg/make_grid_golden.txt。对比矩形序列 + 随机数取数次数。
#include "make_grid.h"

#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

struct GridCase {
  unsigned seed;
  int n;
  Rect bounds;
  int expect_rng;
  const char* rects;   // "x,y,w,h x,y,w,h ..."
};

const GridCase kCases[] = {
    {0x12345678u, 4, {0, 0, 70, 70}, 1,
     "2,2,31,31 37,2,31,31 2,37,31,31 37,37,31,31"},
    {0x12345678u, 5, {0, 0, 70, 70}, 2,
     "2,2,19,31 25,2,19,31 48,2,19,31 13,37,19,31 36,37,19,31"},
    {0x0BADF00Du, 6, {0, 0, 70, 70}, 1,
     "2,2,19,31 25,2,19,31 48,2,19,31 2,37,19,31 25,37,19,31 48,37,19,31"},
    {0x0BADF00Du, 7, {2, 3, 80, 60}, 3,
     "4,5,22,16 30,5,22,16 56,5,22,16 17,25,22,16 43,25,22,16 17,45,22,16 43,45,22,16"},
    {0xDEADBEEFu, 8, {2, 3, 80, 60}, 2,
     "4,5,22,16 30,5,22,16 56,5,22,16 4,25,22,16 30,25,22,16 56,25,22,16 17,45,22,16 43,45,22,16"},
    {0xDEADBEEFu, 9, {0, 0, 120, 120}, 1,
     "2,2,36,36 42,2,36,36 82,2,36,36 2,42,36,36 42,42,36,36 82,42,36,36 2,82,36,36 42,82,36,36 82,82,36,36"},
    {0x5A5A5A5Au, 10, {0, 0, 120, 120}, 3,
     "2,2,36,26 2,32,36,26 2,62,36,26 2,92,36,26 42,17,36,26 42,47,36,26 42,77,36,26 82,17,36,26 82,47,36,26 82,77,36,26"},
    {0x5A5A5A5Au, 11, {5, 5, 64, 48}, 2,
     "7,7,17,8 7,19,17,8 7,31,17,8 7,43,17,8 28,13,17,8 28,25,17,8 28,37,17,8 49,7,17,8 49,19,17,8 49,31,17,8 49,43,17,8"},
    {0x00C0FFEEu, 12, {5, 5, 64, 48}, 1,
     "7,7,17,8 7,19,17,8 7,31,17,8 7,43,17,8 28,7,17,8 28,19,17,8 28,31,17,8 28,43,17,8 49,7,17,8 49,19,17,8 49,31,17,8 49,43,17,8"},
    {0x00000001u, 2, {0, 0, 70, 70}, 1,
     "2,2,66,31 2,37,66,31"},
    {0x00000001u, 3, {0, 0, 70, 70}, 2,
     "2,19,31,31 37,2,31,31 37,37,31,31"},
    {0xFFFFFFFFu, 1, {0, 0, 70, 70}, 1,
     "2,2,66,66"},
    {0x13579BDFu, 12, {0, 0, 70, 70}, 1,
     "2,2,13,19 19,2,13,19 36,2,13,19 53,2,13,19 2,25,13,19 19,25,13,19 36,25,13,19 53,25,13,19 2,48,13,19 19,48,13,19 36,48,13,19 53,48,13,19"},
    {0x2468ACE0u, 9, {1, 1, 33, 97}, 1,
     "3,3,7,28 3,35,7,28 3,67,7,28 14,3,7,28 14,35,7,28 14,67,7,28 25,3,7,28 25,35,7,28 25,67,7,28"},
    {0x0F0F0F0Fu, 16, {0, 0, 100, 100}, 1,
     "2,2,21,21 2,27,21,21 2,52,21,21 2,77,21,21 27,2,21,21 27,27,21,21 27,52,21,21 27,77,21,21 52,2,21,21 52,27,21,21 52,52,21,21 52,77,21,21 77,2,21,21 77,27,21,21 77,52,21,21 77,77,21,21"},
    {0x77777777u, 25, {0, 0, 100, 100}, 1,
     "2,2,16,16 2,22,16,16 2,42,16,16 2,62,16,16 2,82,16,16 22,2,16,16 22,22,16,16 22,42,16,16 22,62,16,16 22,82,16,16 42,2,16,16 42,22,16,16 42,42,16,16 42,62,16,16 42,82,16,16 62,2,16,16 62,22,16,16 62,42,16,16 62,62,16,16 62,82,16,16 82,2,16,16 82,22,16,16 82,42,16,16 82,62,16,16 82,82,16,16"},
};

std::string Format(const std::vector<Rect>& v) {
  std::ostringstream os;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) os << ' ';
    os << v[i].x << ',' << v[i].y << ',' << v[i].w << ',' << v[i].h;
  }
  return os.str();
}

struct GridRun {
  std::vector<Rect> rects;
  long long draws;
};

GridRun RunCase(const GridCase& c) {
  FpuChopScope chop;
  Randomizer rng(c.seed);
  rng.reset_call_count();
  GridRun r;
  MakeGrid(&r.rects, c.n, c.bounds, rng);
  r.draws = rng.call_count();
  return r;
}

TEST(MakeGrid, RectsMatchOriginal) {
  for (const GridCase& c : kCases) {
    const GridRun r = RunCase(c);
    EXPECT_EQ(Format(r.rects), std::string(c.rects))
        << "seed 0x" << std::hex << c.seed << " n=" << std::dec << c.n;
  }
}

TEST(MakeGrid, RandomDrawCountMatchesOriginal) {
  for (const GridCase& c : kCases) {
    const GridRun r = RunCase(c);
    EXPECT_EQ(r.draws, c.expect_rng)
        << "seed 0x" << std::hex << c.seed << " n=" << std::dec << c.n;
  }
}

// 产出的记录数恒等于 n——文档 §三 的那条论证，这里当性质测试钉住。
TEST(MakeGrid, AlwaysEmitsExactlyNRects) {
  FpuChopScope chop;
  Randomizer rng(0xA5A5A5A5u);
  for (int n = 1; n <= 64; ++n) {
    std::vector<Rect> v;
    MakeGrid(&v, n, Rect{0, 0, 120, 96}, rng);
    EXPECT_EQ(static_cast<int>(v.size()), n) << "n=" << n;
  }
}

}  // namespace
}  // namespace rmg
}  // namespace ra2
