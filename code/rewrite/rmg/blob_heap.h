// 涂块核心（0x59BBC0）用的节点池 + 一基最小堆。
// 堆序**就是**陆地的生长顺序，所以这里的每一条比较语义都直接决定地图字节。
// 证据：下沉 0x5AD870（未内联）；上浮与弹出在 0x59C23C / 0x59BF75 处内联。
// 详见 docs/random-map/stage-seeding-water.md §四。
#ifndef RA2_RMG_BLOB_HEAP_H_
#define RA2_RMG_BLOB_HEAP_H_

#include <cstdint>
#include <vector>

namespace ra2 {
namespace rmg {

// 原版是 8 字节：{int16 x, int16 y, float priority}。坐标必须是 16 位——
// 0x59C37D 用 movsx 从这里读回等距坐标，宽一点就不是同一个数了。
struct PaintNode {
  std::int16_t x = 0;
  std::int16_t y = 0;
  float priority = 0.0f;
};

// 节点池：原版一次性 malloc(poolCap * 8)，只前进不回收。
// 池满之后候选格会被**直接丢弃**（连带 work[].queued 已经被写过了），照抄。
class NodePool {
 public:
  explicit NodePool(int capacity) : cap_(capacity) { nodes_.reserve(capacity); }

  int capacity() const { return cap_; }
  int used() const { return static_cast<int>(nodes_.size()); }
  bool Full() const { return used() >= cap_; }

  // 满时返回 -1；调用方据此跳过该候选格（0x59C1xx 的 continue）。
  int Alloc(int x, int y, float priority) {
    if (Full()) return -1;
    PaintNode n;
    n.x = static_cast<std::int16_t>(x);
    n.y = static_cast<std::int16_t>(y);
    n.priority = priority;
    nodes_.push_back(n);
    return static_cast<int>(nodes_.size()) - 1;
  }

  const PaintNode& operator[](int i) const { return nodes_[i]; }
  PaintNode& operator[](int i) { return nodes_[i]; }

 private:
  int cap_;
  std::vector<PaintNode> nodes_;
};

// 一基二叉最小堆。原版数组里存的是 PaintNode*，这里存池下标——
// 唯一有语义的是比较结果，指针值本身只被用来更新两个没人读的统计字段
// （堆结构 +0x0C / +0x10，见 0x59C28B-0x59C2A1）。
class MinHeap {
 public:
  MinHeap(NodePool* pool, int capacity) : pool_(pool), cap_(capacity), slot_(capacity + 1, -1) {}

  int size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // 0x59C23C 起的内联上浮。
  // ⚠️ 两处必须照抄：
  //   1) 门槛是 `count + 1 >= cap`，于是**最后一格永远用不上**，实际容量是 cap-1；
  //   2) 越界时**静默丢弃**，不报错也不回退调用方已经写下的 work[].queued。
  // 返回是否真的入堆（原版不看返回值，这里留给测试断言用）。
  bool Push(int node) {
    int i = count_ + 1;
    if (i >= cap_) return false;             // 0x59C250 cmp ecx,edi / jae
    const float pr = (*pool_)[node].priority;
    int parent = static_cast<int>(static_cast<unsigned>(i) >> 1);
    while (i > 1) {                          // 0x59C25C cmp ecx,1 / jbe
      const int up = slot_[parent];
      // 0x59C26A fcomp + test ah,0x41 / jne：父 <= 新值就停。相等也停。
      if (!((*pool_)[up].priority > pr)) break;
      slot_[i] = up;
      i = parent;
      parent = static_cast<int>(static_cast<unsigned>(i) >> 1);
    }
    slot_[i] = node;
    ++count_;
    return true;
  }

  // 0x59BF75 起的内联弹出：取 arr[1]，把末项搬到根，末槽清零，count--，再 SiftDown(1)。
  // 空堆返回 -1（原版返回 NULL 指针）。
  int Pop() {
    if (count_ == 0) return -1;
    const int top = slot_[1];
    slot_[1] = slot_[count_];
    slot_[count_] = -1;
    --count_;
    SiftDown(1);
    return top;
  }

  // 0x5AD870，__thiscall(this, int i)。全部比较都是**严格大于**，相等不交换。
  void SiftDown(int i) {
    for (;;) {
      const int l = 2 * i, r = 2 * i + 1;
      int best = i;
      // 0x5AD881：先跟左孩子比，父 > 左则 best = 左
      if (l <= count_ && (*pool_)[slot_[i]].priority > (*pool_)[slot_[l]].priority) best = l;
      // 0x5AD89F：再拿 best 跟右孩子比
      if (r <= count_ && (*pool_)[slot_[best]].priority > (*pool_)[slot_[r]].priority) best = r;
      if (best == i) return;
      const int t = slot_[i];
      slot_[i] = slot_[best];
      slot_[best] = t;
      i = best;
    }
  }

  // 以下三个只给回归用：原版的堆数组是模拟器直接摆出来的，
  // 要对拍 SiftDown 就得能绕过 Push 摆一个任意（未必合法）的堆。
  int PeekAt(int i) const { return slot_[i]; }
  void RawPlace(int i, int node) { slot_[i] = node; }
  void RawSetCount(int n) { count_ = n; }

 private:
  NodePool* pool_;
  int cap_;
  int count_ = 0;
  std::vector<int> slot_;
};

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_BLOB_HEAP_H_
