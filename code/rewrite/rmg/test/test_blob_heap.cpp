// 涂块最小堆的回归。堆序 = 陆地生长顺序，所以这里错一位，整张图就散了。
//
// 取证强度分两级，别混为一谈：
//   * SiftDown —— Unicorn 直接执行原版 0x5AD870 取的黄金值，48 例（下面的表）
//   * Push/Pop —— 原版内联在 0x59C23C / 0x59BF75，**没法单独调用**，
//                 只能逐条抄汇编，再用结构不变量与两条已实锤的怪癖兜底
#include <gtest/gtest.h>

#include <vector>

#include "blob_heap.h"

using ra2::rmg::MinHeap;
using ra2::rmg::NodePool;

namespace {

struct SiftCase {
  int i;
  std::vector<float> before;   // 1 基，这里从下标 1 起列
  std::vector<int> after;      // 下沉后每个槽位放的是原来的第几号
};

// 一半用例只用 {0,1,2} 三个优先级：并列是常态，必须钉死「相等不交换」
const SiftCase kSift[] = {
    {7, {1.0f, 1.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f}, {1, 2, 3, 4, 5, 6, 7}},
    {4, {7.3f, 1.5f, 0.3f, 0.3f, 2.5f, 8.1f, 4.1f, 8.1f, 2.5f, 6.4f, 7.3f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {2, {0.0f, 2.0f}, {1, 2}},
    {4, {1.9f, 6.3f, 6.3f, 1.9f, 1.9f}, {1, 2, 3, 4, 5}},
    {2, {0.0f, 1.0f}, {1, 2}},
    {6, {0.7f, 4.1f, 2.6f, 0.7f, 2.8f, 1.5f, 1.9f, 4.5f, 0.7f, 4.7f, 7.8f, 2.8f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {3, {1.0f, 2.0f, 0.0f, 2.0f}, {1, 2, 3, 4}},
    {6, {5.9f, 5.9f, 7.7f, 1.9f, 7.8f, 1.7f, 5.9f}, {1, 2, 3, 4, 5, 6, 7}},
    {9, {1.0f, 0.0f, 2.0f, 1.0f, 1.0f, 1.0f, 2.0f, 1.0f, 0.0f, 2.0f, 2.0f, 1.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {5, {1.6f, 1.6f, 1.9f, 1.9f, 4.1f}, {1, 2, 3, 4, 5}},
    {1, {2.0f, 0.0f, 1.0f, 2.0f, 2.0f}, {2, 1, 3, 4, 5}},
    {12, {2.4f, 8.7f, 2.8f, 4.3f, 1.5f, 4.3f, 1.5f, 1.3f, 4.3f, 7.6f, 0.3f, 0.3f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {2, {0.0f, 2.0f}, {1, 2}},
    {6, {5.7f, 2.5f, 7.0f, 1.2f, 8.5f, 5.7f}, {1, 2, 3, 4, 5, 6}},
    {8, {2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f}, {1, 2, 3, 4, 5, 6, 7, 8}},
    {2, {2.9f, 2.9f}, {1, 2}},
    {4, {1.0f, 0.0f, 0.0f, 2.0f, 1.0f, 2.0f}, {1, 2, 3, 4, 5, 6}},
    {7, {2.2f, 1.6f, 3.3f, 2.2f, 4.4f, 2.2f, 7.9f, 3.3f, 3.4f, 2.2f, 7.6f, 1.5f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {2, {2.0f, 1.0f, 0.0f, 2.0f, 0.0f, 0.0f}, {1, 5, 3, 4, 2, 6}},
    {1, {1.2f, 2.1f, 5.1f, 3.8f, 3.8f}, {1, 2, 3, 4, 5}},
    {1, {2.0f, 2.0f, 2.0f, 1.0f, 0.0f, 2.0f, 2.0f, 2.0f, 0.0f, 0.0f, 1.0f, 0.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {12, {8.7f, 2.6f, 4.2f, 7.9f, 0.3f, 0.3f, 4.2f, 8.7f, 1.0f, 7.9f, 8.9f, 4.2f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {1, {2.0f, 1.0f, 0.0f, 0.0f, 2.0f, 1.0f, 2.0f}, {3, 2, 6, 4, 5, 1, 7}},
    {8, {0.2f, 4.4f, 6.0f, 0.2f, 7.7f, 4.5f, 4.9f, 4.9f, 0.2f, 6.0f, 2.1f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {7, {2.0f, 2.0f, 1.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f}, {1, 2, 3, 4, 5, 6, 7, 8}},
    {1, {3.6f, 6.2f, 2.9f, 5.9f, 2.3f, 2.9f, 6.3f, 6.3f, 2.3f, 2.6f}, {3, 2, 6, 4, 5, 1, 7, 8, 9, 10}},
    {6, {0.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.0f, 2.0f, 2.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {10, {1.4f, 7.8f, 4.8f, 1.2f, 1.4f, 7.5f, 7.8f, 4.2f, 3.0f, 1.9f, 3.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {2, {0.0f, 2.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2.0f}, {1, 4, 3, 2, 5, 6, 7, 8}},
    {4, {7.0f, 4.4f, 4.4f, 3.7f, 7.0f, 3.7f, 3.9f}, {1, 2, 3, 4, 5, 6, 7}},
    {4, {2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 1.0f}, {1, 2, 3, 4, 5, 6, 7}},
    {5, {1.6f, 8.2f, 1.6f, 5.7f, 1.6f, 5.7f}, {1, 2, 3, 4, 5, 6}},
    {8, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {1, {0.7f, 0.7f}, {1, 2}},
    {4, {0.0f, 2.0f, 0.0f, 0.0f, 0.0f}, {1, 2, 3, 4, 5}},
    {2, {1.8f, 4.1f, 8.8f, 0.9f, 0.9f, 4.1f}, {1, 4, 3, 2, 5, 6}},
    {5, {0.0f, 2.0f, 1.0f, 2.0f, 0.0f}, {1, 2, 3, 4, 5}},
    {8, {5.4f, 3.1f, 1.6f, 5.4f, 5.4f, 1.5f, 1.9f, 7.7f, 1.6f, 5.8f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
    {6, {2.0f, 0.0f, 2.0f, 2.0f, 0.0f, 2.0f, 0.0f, 1.0f, 1.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {2, {8.8f, 7.1f, 8.8f, 6.6f, 8.8f}, {1, 4, 3, 2, 5}},
    {3, {0.0f, 1.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f}, {1, 2, 7, 4, 5, 6, 3}},
    {5, {2.0f, 9.0f, 0.9f, 6.2f, 0.9f, 7.4f, 7.4f, 9.0f, 6.2f}, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {3, {2.0f, 2.0f, 2.0f, 0.0f}, {1, 2, 3, 4}},
    {9, {1.9f, 4.3f, 1.6f, 1.6f, 1.6f, 8.1f, 2.7f, 5.5f, 1.7f}, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    {3, {0.0f, 2.0f, 1.0f, 1.0f, 2.0f}, {1, 2, 3, 4, 5}},
    {6, {6.7f, 6.7f, 8.7f, 6.4f, 8.7f, 6.7f, 3.9f, 4.1f, 0.8f, 4.1f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}},
    {7, {2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.0f, 2.0f, 0.0f, 2.0f, 0.0f, 1.0f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
    {2, {2.2f, 2.2f, 7.9f, 3.9f, 5.0f, 3.9f, 3.9f, 7.9f, 3.9f, 3.3f, 2.8f}, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
};

// 按优先级表建一个「按原顺序摆放」的堆，再对第 i 个槽位下沉
void Build(NodePool* pool, MinHeap* heap, const std::vector<float>& pri) {
  pool->Alloc(0, 0, -1.0f);   // 占位：让池下标与 1 基的堆槽位号对齐（模拟器也是这么摆的）
  for (std::size_t k = 0; k < pri.size(); ++k) {
    pool->Alloc(static_cast<int>(k), static_cast<int>(k), pri[k]);
    heap->RawPlace(static_cast<int>(k) + 1, static_cast<int>(k) + 1);
  }
  heap->RawSetCount(static_cast<int>(pri.size()));
}

TEST(BlobHeap, SiftDownMatchesOriginal) {
  int idx = 0;
  for (const SiftCase& c : kSift) {
    NodePool pool(64);
    MinHeap heap(&pool, 64);
    Build(&pool, &heap, c.before);
    heap.SiftDown(c.i);
    for (std::size_t k = 0; k < c.after.size(); ++k) {
      EXPECT_EQ(heap.PeekAt(static_cast<int>(k) + 1), c.after[k])
          << "第 " << idx << " 例，槽位 " << (k + 1);
    }
    ++idx;
  }
}

// 相等不交换：全相同的堆，下沉必须原地不动
TEST(BlobHeap, EqualPrioritiesNeverSwap) {
  NodePool pool(32);
  MinHeap heap(&pool, 32);
  Build(&pool, &heap, {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
  for (int i = 7; i >= 1; --i) heap.SiftDown(i);
  for (int i = 1; i <= 7; ++i) EXPECT_EQ(heap.PeekAt(i), i);
}

// Pop 必须按优先级不减序吐出来
TEST(BlobHeap, PopYieldsNonDecreasing) {
  NodePool pool(300);
  MinHeap heap(&pool, 300);
  std::uint32_t s = 12345;
  for (int k = 0; k < 200; ++k) {
    s = s * 1103515245u + 12345u;
    heap.Push(pool.Alloc(k, k, static_cast<float>((s >> 16) % 50)));
  }
  float last = -1.0f;
  int n = 0;
  for (int id = heap.Pop(); id >= 0; id = heap.Pop()) {
    EXPECT_GE(pool[id].priority, last);
    last = pool[id].priority;
    ++n;
  }
  EXPECT_EQ(n, 200);
}

// 怪癖一：门槛写的是 `count + 1 >= cap`，所以最后一格永远空着，实际容量是 cap-1
TEST(BlobHeap, LastSlotIsNeverUsed) {
  NodePool pool(16);
  MinHeap heap(&pool, 8);
  int accepted = 0;
  for (int k = 0; k < 16; ++k)
    if (heap.Push(pool.Alloc(k, k, static_cast<float>(k)))) ++accepted;
  EXPECT_EQ(accepted, 7);      // 不是 8
  EXPECT_EQ(heap.size(), 7);
}

// 怪癖二：堆满时静默丢弃——不报错，也不撤销调用方已经写下的 work[].queued
TEST(BlobHeap, OverflowSilentlyDrops) {
  NodePool pool(16);
  MinHeap heap(&pool, 4);
  for (int k = 0; k < 3; ++k) EXPECT_TRUE(heap.Push(pool.Alloc(k, k, 9.0f)));
  // 这一个优先级最低，本该立刻被弹出来，但因为堆已满，它就这么没了
  EXPECT_FALSE(heap.Push(pool.Alloc(99, 99, 0.0f)));
  EXPECT_EQ(pool[heap.PeekAt(1)].priority, 9.0f);
}

// 节点池满了返回 -1，调用方据此跳过该候选格
TEST(BlobHeap, NodePoolExhaustionReportsMinusOne) {
  NodePool pool(3);
  EXPECT_GE(pool.Alloc(1, 1, 0.0f), 0);
  EXPECT_GE(pool.Alloc(2, 2, 0.0f), 0);
  EXPECT_GE(pool.Alloc(3, 3, 0.0f), 0);
  EXPECT_EQ(pool.Alloc(4, 4, 0.0f), -1);
  EXPECT_TRUE(pool.Full());
}

// 坐标存成 int16：0x59C37D 用 movsx 读回来，宽度不对就不是同一个数
TEST(BlobHeap, CoordsAreInt16) {
  NodePool pool(4);
  const int id = pool.Alloc(70, 140, 1.0f);
  EXPECT_EQ(pool[id].x, 70);
  EXPECT_EQ(pool[id].y, 140);
  EXPECT_EQ(sizeof(ra2::rmg::PaintNode), 8u);
}

}  // namespace
