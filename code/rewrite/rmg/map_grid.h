// 等距地图的格子集合与「扫全图」的遍历顺序
//
// 这是所有地形阶段的地基：阶段 1 清格子、阶段 2 铺水、岸线平滑的四遍、
// Recalc 的四轮……全都是「按同一个顺序扫全图」。顺序错了，同一个随机数
// 会落到别的格子上，整张地图就散了。
//
// 取证（gamemd.exe）：
//   MapClass::CellIterator_Reset  0x578350
//   MapClass::CellIterator_Next   0x578290
//   菱形边界 [0xABED04]=W / [0xABED08]=W+2H，写入点 0x5662FB-0x566317
//   边界判定 0x57A3C6-0x57A3EF 与 0x57B254-0x57B271（两处完全一致）
// 详见 docs/random-map/map-geometry.md。
#pragma once

#include <cstdint>

namespace ra2::rmg {

// 格坐标是**等距坐标**，不是真实坐标。原版把它打包成一个 dword：
// 低 16 位 = X，高 16 位 = Y（工作记录 +0x00 与掩码函数的取法都是这样）。
struct CellCoord {
  int x = 0;
  int y = 0;

  std::uint32_t Packed() const {
    return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16) |
           static_cast<std::uint16_t>(x);
  }
  static CellCoord FromPacked(std::uint32_t v) {
    return CellCoord{static_cast<std::int16_t>(v & 0xFFFF),
                     static_cast<std::int16_t>(v >> 16)};
  }
  bool operator==(const CellCoord& o) const { return x == o.x && y == o.y; }
};

class MapGrid {
 public:
  MapGrid(int width, int height) : w_(width), h_(height) {}

  int width() const { return w_; }
  int height() const { return h_; }

  // 工作数组的边长与下标（0x42AC5C 定义 side；0x5A00C0/0x5A0090 用 Y*side+X）
  int side() const { return w_ + h_ + 1; }
  int WorkIndex(const CellCoord& c) const { return c.y * side() + c.x; }

  // 菱形内判定。四个条件逐条对应 0x57A3C6 起那四条比较，**注意都是严格/非严格
  // 各按原样**：X+Y 必须 > W（不是 >=），且 <= W+2H（不是 <）。
  bool InMap(int x, int y) const {
    return (x + y) > w_ && (y - x) < w_ && (x - y) < w_ && (x + y) <= w_ + 2 * h_;
  }
  bool InMap(const CellCoord& c) const { return InMap(c.x, c.y); }

  // 菱形里的格子总数 = H * (2W - 1)。
  // 注意它**不等于** 2WH，也不等于阶段 1 那个 (H+4)*W*2——后者是原版故意放宽的
  // 覆盖率分母，见 stage-init.md §六。
  int CellCount() const { return h_ * (2 * w_ - 1); }

  // 按原版顺序扫全图：起点 (1, W)，每步 (X+1, Y-1)；一行走完后
  // 交换 X/Y 并按 (Y-W+X-1) 的奇偶决定下一行的起点与长度。
  // 原版靠「格子指针为 NULL」停机，等价于走出菱形即停。
  template <typename Fn>
  void ForEachCell(Fn&& fn) const {
    int x = 1, y = w_, count = w_ - 1;
    while (InMap(x, y)) {
      fn(CellCoord{x, y});
      if (count != 0) {
        ++x;
        --y;
        --count;
      } else {
        const int ox = x, oy = y;
        const int t = (oy - w_) + ox - 1;   // 0x5782F8 lea edx,[edx+eax-1]
        if (t & 1) {                        // 0x578301 起
          x = oy;
          y = ox + 1;
          count = w_ - 1;
        } else {                            // 0x57830B 起
          x = oy + 1;
          y = ox;
          count = w_ - 2;
        }
      }
    }
  }

 private:
  int w_;
  int h_;
};

}  // namespace ra2::rmg
