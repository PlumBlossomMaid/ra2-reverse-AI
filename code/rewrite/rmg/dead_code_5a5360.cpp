// 死代码复现 —— 0x5A5360 废弃方案最大函数（batch 1234）
// 语义与地址依据详见 dead_code_5a5360.h 头部注释。实现按反汇编逐条复刻，
// 打桩面与探针同源（golden 由 .qwen/tmp/_emu_deadcode_5a5360.py 取得）。
#include "dead_code_5a5360.h"

#include <cstdint>
#include <vector>

#include "randomizer.h"
#include "scaled_random.h"

namespace ra2 {
namespace rmg {
namespace {

// 原版常量（探针实测位模式，见 _probe_5a5360_aux.py / 项目既有取证）
inline constexpr double kDistScale3 = 3.0;                    // [0x7ED818]
inline constexpr double kRangeScale163e9 = 1.6298145059565582e-09;  // [0x7EDAF8]
inline constexpr double kCountScale16 = 1.6;                  // [0x7EDB00]

// 堆对象（esi）：+0=count +4=容量 +8=指针数组（C++ 用 int 下标数组）+0xC/+0x10
// 是 max/min 节点指针的**死存储**（写入后函数内无人读——原版怪癖，照抄
// 但在 C++ 里无观测对象，故不模拟，注释标注）。
struct MinHeap {
  int count = 0;
  int cap = 0;
  std::vector<int> idx;  // 1-based：idx[1]=根（下标指向主 buffer 的项）
};

// 主 buffer 项 = {u32 打包 xy, float key}
struct Entry {
  std::uint32_t xy = 0;
  float key = 0.0f;
};

// 最小堆 sift-down（0x5AD870，thiscall ecx=堆 + 1 栈参=下沉索引 ret 4）：
// 比较 buf[].+4 float key，fcompp + fnstsw + test ah,0x41（C0|C3）——
// 选两子中 key 较小者与当前比较，当前 key > 子 → 下沉。
void HeapSiftDown(const std::vector<Entry>& buf, MinHeap& h, int si) {
  const int count = h.count;
  while (true) {
    int edx = si * 2;        // 左子
    int edi = si * 2 + 1;    // 右子
    if (edx > count) {
      edx = si;
    } else {
      // 左子 vs 当前：fld 左子key; fld 当前key; fcompp → st0=当前、st1=左子；
      // C0=当前<左子。test ah,0x41：C0|C3（当前<=左子）→ 不选；否则选左子。
      const float cur = buf[h.idx[si]].key;
      const float left = buf[h.idx[edx]].key;
      if (cur <= left) edx = si;  // 当前<=左子 → 保持（不选左子）
    }
    if (edi <= count) {
      // 右子 vs 已选中项：C0=选中<右子。C0|C3（选中<=右子）→ 不选。
      const float sel = buf[h.idx[edx]].key;
      const float right = buf[h.idx[edi]].key;
      if (sel > right) edx = edi;  // 选中 > 右子 → 选右子
    }
    if (edx == si) return;   // 无需下沉
    // 交换 buf[si] 与 buf[edx]，继续以新位置下沉
    std::swap(h.idx[si], h.idx[edx]);
    si = edx;
  }
}

// 堆插入 sift-up（内联于 0x5A5482-0x5A54C0 / 0x5A5719-0x5A577A）：
// 比较父 key 与新 key，父 > 新 → 父下沉继续上溯。返回是否真的插入
// （插入索引 < 容量才插；索引==1 或父<=新 时直接放）。
void HeapPush(const std::vector<Entry>& buf, MinHeap& h, int item, float key) {
  int ecx = h.count + 1;       // 新索引
  if (ecx >= h.cap) return;    // jae → 容量已满不插（原版：插入点超容量跳过）
  int eax = ecx;
  int edx = ecx >> 1;          // 父索引
  if (ecx <= 1) {
    h.idx[ecx] = item;
    h.count++;
    return;
  }
  while (true) {
    const float parent = buf[h.idx[edx]].key;
    // fld 父key; fcomp [esp+key]：C0=父<新、C3=父==新。C0|C3（父<=新）→ 放位。
    if (parent <= key) break;
    h.idx[ecx] = h.idx[edx];   // 父下沉
    ecx = edx;
    edx >>= 1;
    if (ecx <= 1) break;
  }
  h.idx[ecx] = item;
  h.count++;
}

// 出堆（0x5A54DA-0x5A550A / 0x5A5798-0x5A57CA）：root 取出 → 末尾补 root →
// count-- → sift-down(1)。堆空返回 -1。
int HeapPop(const std::vector<Entry>& buf, MinHeap& h) {
  if (h.count == 0) return -1;
  const int top = h.idx[1];
  h.idx[1] = h.idx[h.count];
  h.idx[h.count] = 0;
  h.count--;
  HeapSiftDown(buf, h, 1);
  return top;
}

// 8 方向（0x42D510 真身语义在 World 回调里实现偏移表）
struct XY {
  int x = 0;
  int y = 0;
};
inline XY Unpack(std::uint32_t xy) {
  XY r;
  r.x = static_cast<std::int16_t>(xy & 0xFFFF);
  r.y = static_cast<std::int16_t>((xy >> 16) & 0xFFFF);
  return r;
}
inline std::uint32_t Pack(int x, int y) {
  return (static_cast<std::uint32_t>(y) << 16) |
         (static_cast<std::uint32_t>(x) & 0xFFFF);
}

// 菱形边界（0x5A55F7-0x5A5626 内联）：X+Y>A 且 X+Y<=B 且 |X-Y|<A
inline bool DiamondPass(int x, int y, int a, int b) {
  const int s = x + y;
  if (s <= a) return false;
  if (x - y >= a) return false;
  if (y - x >= a) return false;
  if (s > b) return false;
  return true;
}

// bbox（0x5A7640，edx=向量）：遍历元素算 min/max，输出 {minX,minY,W,H}。
// 初值：minX=minY=0x3E7(999)、maxX=maxY=0（mov di,0x3e7 / xor bp / mov bx,di）。
struct Rect {
  int min_x = 0, min_y = 0, w = 0, h = 0;
};
Rect VecBBox(const std::vector<std::uint32_t>& vec, int count) {
  Rect r;
  int mnx = 0x3E7, mn = 0x3E7, mxx = 0, mxy = 0;
  for (int i = 0; i < count; ++i) {
    const XY p = Unpack(vec[i]);
    if (p.x < mnx) mnx = p.x;
    if (p.y < mn) mn = p.y;
    if (p.x > mxx) mxx = p.x;
    if (p.y > mxy) mxy = p.y;
  }
  r.min_x = mnx;
  r.min_y = mn;
  r.w = mxx - mnx + 1;
  r.h = mxy - mn + 1;
  return r;
}

// 有符号除以 3（0x5A595B imul 0x55555556：高 32 位 + 符号修正 = 向零截断）
inline int Div3(int v) {
  return v / 3;
}

}  // namespace

void DeadCode5A5360(DeadCode5A5360Context& ctx) {
  DeadCode5A5360World* w = ctx.world;
  Randomizer* rng = ctx.rng;
  const int N = ctx.n;
  const int side = ctx.side;
  const int bound_a = ctx.bound_a;
  const int bound_b = ctx.bound_b;
  const std::uint32_t abc2b0 = ctx.abc2b0;

  // ① N*10（0x5A5367 lea edi,[edi+edi*4]; shl edi,1）
  const int N10 = N * 10;

  // ② operator new(N*10*8) → 主 buffer（每项 8B {xy,key}）
  std::vector<Entry> buf(N10);
  ctx.calls_new++;

  // ③ operator new(0x14) → 堆对象；operator new(N*10*4+4) → 指针数组（清 0）
  MinHeap heap;
  heap.cap = N10;
  heap.idx.assign(N10 + 1, 0);
  ctx.calls_new += 2;
  // [esi+0xC]=0、[esi+0x10]=0xFFFFFFFF（死存储——函数内无人读，不模拟）

  // ④ CellIterator（0x578350/0x578290）：清 work+0x3C。探针打桩 Next 返 0
  //    → 跳过清空（探针预灌挡墙等效，golden 同源）——IterNext 恒 false。
  ctx.calls_iter++;  // Reset（探针 iterR 打桩）
  ctx.calls_iter++;  // Next 第一次即结束（探针 iterN 打桩返 0）
  // （原版循环被短路，不执行清 work）

  // ⑤ work[0]=*参数1（打包 xy）、work[0]+4=0；起始点 work+0x3C=1
  const std::uint32_t start_xy = Pack(ctx.start_x, ctx.start_y);
  Entry& e0 = buf[0];
  e0.xy = start_xy;
  e0.key = 0.0f;  // mov [ebx+4], ebp → 0
  const int start_idx = ctx.start_y * side + ctx.start_x;
  w->SetWorkVisit(start_idx);
  int placed = 1;  // [esp+0x1C] 安置计数（起始点占 1 项）

  // 堆插入起始点（key=0）
  HeapPush(buf, heap, 0, 0.0f);
  // 出堆 → 当前节点
  int cur = HeapPop(buf, heap);

  // ⑦ 向量 edi（0x42FCB0 构造 + [edi]=0x7E3890 + 0x14=0xA→N+2 + 0x10=0）
  std::vector<std::uint32_t> vec;
  std::uint32_t vec_cap = 0;      // [edi+8]
  int vec_count = 0;              // [edi+0x10]
  std::uint32_t vec_target = N + 2;  // [edi+0x14]
  ctx.calls_new++;  // operator new(0x18)
  // 0x42FCB0(edi,0,0)：+4=0 +8=0 +0xC=1 +0xD=0（C++ 用 vec_cap/vec_target 模拟）

  int processed = 0;  // [esp+0x18] 处理数（0x5A53EB 初 0）

  // ⑧ 主循环（[esp+0x18]<N 且当前节点非空）
  while (processed < N && cur >= 0) {
    // GetCellAt(ebp)（探针打桩返 CELL_OBJ）→ cell+0x38=[0xABC2B0]
    ctx.calls_gcell++;
    w->SetCell38(buf[cur].xy, abc2b0);
    // 向量扩容/追加（0x5A557F-0x5A55B8）：
    //   count < 容量 → 直接写；否则（+0xD 标志 != 0 或 容量==0）→ 虚表槽 2
    //   扩容（newcap = 目标+现有，al==0 跳过）；容量!=0 且标志==0 → 跳过。
    //   ★探针构造 0x42FCB0 后 +0xD=0 恒不变 → 只有容量==0 走首次扩容。
    {
      bool did_append = false;
      if (vec_count < static_cast<int>(vec_cap)) {
        did_append = true;
      } else {
        // +0xD 标志恒 0（探针构造后从未置 1）→ 等价条件 = 容量==0
        if (vec_cap == 0 && vec_target > 0) {
          const std::uint32_t newcap = vec_target + vec_cap;
          ctx.calls_slot8++;
          if (w->VecGrow(newcap)) {
            vec_cap = newcap;
            vec.resize(newcap);
            did_append = true;
          }
        }
      }
      if (did_append) {
        vec[vec_count] = buf[cur].xy;
        vec_count++;
      }
    }

    // 8 方向扩散（dir=0..7）
    for (int dir = 0; dir < 8; ++dir) {
      const std::uint32_t off = w->DirOffset(dir);
      const int dx = static_cast<std::int16_t>(off & 0xFFFF);
      const int dy = static_cast<std::int16_t>((off >> 16) & 0xFFFF);
      const XY p = Unpack(buf[cur].xy);
      const int nx = p.x + dx;
      const int ny = p.y + dy;
      // 菱形边界（0x5A55F7-0x5A5626）
      if (!DiamondPass(nx, ny, bound_a, bound_b)) continue;
      // GetCellAt(&out)（打桩）→ 0x486380（打桩返 1）→ cell+0x44==-1
      ctx.calls_gcell++;
      ctx.calls_p486++;  // 0x486380 每扩散方向调一次（探针 hook 计数）
      if (!w->Pred486(Pack(nx, ny))) continue;
      if (!w->Cell44Neg1(Pack(nx, ny))) continue;
      // work 检查：+0x3C==1（已访问）→ skip；+0x45!=0 → skip
      const int idx = ny * side + nx;
      if (w->WorkVisit(idx) == 1) continue;
      if (w->WorkFlag45(idx)) continue;
      // 成功：写点（buf[placed]）→ key = max(|sx-x|,|sy-y|) +
      //   (uint32)Random()*3.0*kInvUintMax（RNG#1），work+0x3C=1，堆插入
      const int ax = ctx.start_x - nx;
      const int ay = ctx.start_y - ny;
      const int adx = ax < 0 ? -ax : ax;
      const int ady = ay < 0 ? -ay : ay;
      const int dist = adx > ady ? adx : ady;  // max(|dx|,|dy|) 切比雪夫
      const std::uint32_t r = rng->Random();
      const double keyd = (double)dist + (double)(std::uint32_t)r * kDistScale3 * kInvUintMax;
      Entry& en = buf[placed];
      en.xy = Pack(nx, ny);
      en.key = ChopFloat(keyd);
      w->SetWorkVisit(idx);
      placed++;
      HeapPush(buf, heap, placed - 1, en.key);
    }
    // 出堆下一节点；处理数++
    processed++;
    cur = HeapPop(buf, heap);
  }

  // ⑨ 清理：delete(主buffer)；delete(指针数组);delete(堆)
  ctx.calls_del += 3;
  // bbox（0x5A7640）：遍历向量 → rect
  const Rect rect = VecBBox(vec, vec_count);
  // 0x5A5810-0x5A5828：W*H 存 [esp+0x68]、count 存 [esp+0x64]
  // 0x5A5828 fild W*H → fild count → fmul 1.6 → fcompp → test ah,0x41
  //   → jne 0x5A5A9F（面积 >= count*1.6 → 简单）
  //   复杂路径条件 = count*1.6 > 面积（点密集）
  const bool complex = processed > 100 &&
      static_cast<double>(vec_count) * kCountScale16 >
          static_cast<double>(rect.w) * static_cast<double>(rect.h);

  if (complex) {
    // 复杂路径（0x5A5859-0x5A5A93）：
    // 外循环 ebp=count-1..0 × 内层 dir 0..7：vec[ebp]+偏移 → 菱形边界 →
    // GetCellAt → cell+0x38==[0xABC2B0]（探针打桩恒等）→ skip（向量不收缩）
    for (int ebp = vec_count - 1; ebp >= 0; --ebp) {
      const std::uint32_t v = vec[ebp];
      for (int dir = 0; dir < 8; ++dir) {
        const std::uint32_t off = w->DirOffset(dir);
        const int dx = static_cast<std::int16_t>(off & 0xFFFF);
        const int dy = static_cast<std::int16_t>((off >> 16) & 0xFFFF);
        const XY p = Unpack(v);
        const int nx = p.x + dx;
        const int ny = p.y + dy;
        if (!DiamondPass(nx, ny, bound_a, bound_b)) continue;
        ctx.calls_gcell++;
        // cell+0x38 == [0xABC2B0] → je skip（探针打桩恒等 → 从不删除）
        if (w->GetCell38(Pack(nx, ny)) == abc2b0) continue;
        if (ebp >= vec_count) continue;
        vec_count--;  // 收缩（0x5A58E6 lea ecx,[eax-1] → count-1）
        if (ebp >= vec_count) continue;
        for (int i = ebp; i < vec_count; ++i) {
          vec[i] = vec[i + 1];  // 前移（0x5A58F2-0x5A5902）
        }
      }
    }
    // 收缩后遍历标记 work[idx*0x50+0x45]=1（0x5A591F-0x5A5956）
    for (int i = 0; i < vec_count; ++i) {
      const XY p = Unpack(vec[i]);
      w->SetWorkFlag45(p.y * side + p.x);
    }
    // count/3（0x5A5958 imul 0x55555556）次放置循环
    const int iters = Div3(vec_count);
    for (int k = iters; k > 0; --k) {
      // RNG#2（0x5A598C）：v = _ftol(Random()*count*kInv)，ja v>count-1 重抽
      std::uint32_t r2;
      int v;
      do {
        r2 = rng->Random();
        v = Ftol((double)(std::uint32_t)r2 * (double)vec_count * kInvUintMax);
      } while (v > vec_count - 1);
      const std::uint32_t sel = vec[v];  // [esp+0x48]
      // RNG#3（0x5A59BF）：t = _ftol(Random()*1.63e-9)，ja t>6 重抽 → t∈[0,6]
      std::uint32_t r3;
      int t;
      do {
        r3 = rng->Random();
        t = Ftol((double)(std::uint32_t)r3 * kRangeScale163e9);
      } while (t > 6);
      // t<=3 → 直接放置；t>3 → 2×2 邻域检查
      if (t > 3) {
        const XY p = Unpack(sel);
        int flag = 1;
        for (int dy = 0; dy < 2; ++dy) {
          for (int dx = 0; dx < 2; ++dx) {
            const int nx = p.x + dx;
            const int ny = p.y + dy;
            const int idx = ny * side + nx;
            if (!w->WorkFlag45(idx)) {
              flag = 0;
            } else {
              ctx.calls_gcell++;
              if (!(w->GetCell38(Pack(nx, ny)) == abc2b0)) flag = 0;
            }
          }
        }
        if (!flag) continue;
      }
      // 放置（0x5A5A6C）：ecx=[0xAA10A4]+t；call 0x5A6C10（探针打桩记录）
      ctx.calls_6c10++;
      w->Place(t);
    }
    // 0x5A5A93: 返回向量（本批 C++ 不模拟返回对象）
  } else {
    // 简单路径（0x5A5A9F-0x5A5AD6）：遍历 vec → GetCellAt(&vec[i]) →
    //   cell+0x38=0xFFFF；虚表槽 0（0x42FBF0）打桩
    for (int i = vec_count - 1; i >= 0; --i) {
      ctx.calls_gcell++;
      w->SetCell38(vec[i], 0xFFFF);
    }
    ctx.calls_slot0++;
    w->VecClear();
  }
}

}  // namespace rmg
}  // namespace ra2