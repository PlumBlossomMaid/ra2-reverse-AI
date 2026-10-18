// 死代码复现 —— 0x5A89D0 批量放置主循环（batch 928）
//
// 语义链见 batch_placer.h 头注释。实现要点（全部有汇编依据 + emu 黄金
// 实证，golden 12 用例：deadcode_batch_placer_golden.txt）：
//  - 拒绝采样：Random()（0xABE890）→ fild qword（无符号 32 位）→
//    fmul [0x7EDB28]=11/(2^32-1) → _ftol（RC=chop）→ 若 >10 重抽。
//  - 11 次探测：(采样+计数)%11 与 [0xABAD20]（golden 灌 0 → 索引 0..10）
//    查 0x5A7D00 判定；全失败返回 0。
//  - 环形队列 1024 槽 × 0x14B；count/head/tail。
//  - 主循环只采样 1 次（rng=1 恒定，诊断实证）：入队 1 槽后进入处理循环。
//  - 处理槽：type!=0xFFFF → CleanType + DispatchType + LAT 三档映射
//    （[0xABC1D4]=0 → idx=type）→ [0xABE370] count<=1 不入队；
//    >1 时 operator new 临时数组 n 项 {0xFFFF,0,0xFFFF,0,-1}。
//  - 遍历 0..count-1（循环上界 = 表项 count）：(kx,ky) 子表 + 方向
//    (ky-4)&7 合成目标 → 0x5A8F00 两次（mode=1/4）。第一次失败清 flag
//    break；第二次成功写回临时项 {type←0, x←0x11223344, x2←3}
//    （雪球关键：type 0xFFFF→0 → 批量入队后再展开）；失败回填 x2←3；
//    success 计数成功后必 +1（成功失败都算）。
//  - 收尾：flag && success>0 → 批量入队 success 项（count 上限 0x400）。
//  - 出队 head+1&0x3FF、count--、placed++（每处理 1 槽无条件 +1）。
//  - count>=0x3FC → 0x5A8E7C 排空：逐槽 DispatchType + 出队到 count==0。
//  - 返回 al = (placed > 1) ? 1 : 0（0x5A8EDF cmp [esp+0x30],1 / jle）。
#include "batch_placer.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ra2 {
namespace rmg {

namespace {

// 0x7EDB28：11/(2^32-1) = 2.5611370807888771e-09（fmul qword 读取，实证）
constexpr double kTypeScale = 2.5611370807888771e-09;

// 0x89F688 八方向表：PE 里是 BSS 段（emu 读 0，golden 以 0 为基准）。
// （真机此表从未被写——死代码零引用；若将来证实有值，需回修。）
constexpr std::uint16_t kDirTable[8][2] = {
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
    {0, 0}, {0, 0}, {0, 0}, {0, 0},
};

inline std::uint32_t PackCoord(const CellCoord& c) {
  return (static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.x)) & 0xFFFF) |
         ((static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.y)) & 0xFFFF)
          << 16);
}

}  // namespace

int DeadCodeBatchPlacer(DeadBatchPlacerContext& ctx) {
  Randomizer& rng = *ctx.rng;
  DeadBatchPlacerWorld& world = *ctx.world;

  BatchPlacerSlot ring[kBatchPlacerRingSize];
  int count = 0;    // [esp+0x5C]
  int head = 0;     // [esp+0x60]
  int tail = 0;     // [esp+0x64]
  int placed = 0;   // [esp+0x30]

  // ② 拒绝采样 + 探测（0x5A8A0A）：只执行一次
  int type_index = -1;
  for (;;) {
    std::uint32_t r = rng.Random();
    double scaled = static_cast<double>(r) * kTypeScale;
    int sample = static_cast<int>(std::trunc(scaled));  // _ftol（RC=chop）
    if (sample > 10) {
      continue;  // 0x5A8A2D cmp edi,0xA / ja 重抽
    }
    for (int counter = 0; counter < 11; ++counter) {
      int idx = (sample + counter) % 11;  // [0xABAD20]=0
      if (world.ProbeType(idx, ctx.base)) {
        type_index = idx;
        break;
      }
    }
    break;
  }
  if (type_index < 0) {
    return 0;  // 0x5A8A65：11 次全失败 → xor al,al
  }

  // ③ 入队（0x5A8A74）：槽 = {type, base 打包, 0xFFFF, 同, -1}
  const std::uint32_t base_pack = PackCoord(ctx.base);
  if (count < 0x400) {  // cmp [esp+0x5C],0x400 / jge 跳过
    ring[tail].type = static_cast<std::uint32_t>(type_index);
    ring[tail].x = base_pack;
    ring[tail].sentinel = 0xFFFF;
    ring[tail].x2 = base_pack;
    ring[tail].back = 0xFFFFFFFF;
    (void)world.TimeGetTime();  // 辅助数组只写不读
    tail = (tail + 1) & (kBatchPlacerRingSize - 1);
    ++count;
  }

  // ④ 处理循环（0x5A8AE1）
  for (;;) {
    if (count == 0) {
      break;  // 0x5A8E7A → 返回判断
    }
    if (count >= 0x3FC) {  // jge 0x5A8E7C 排空
      while (count > 0) {
        BatchPlacerSlot& s = ring[head];
        world.DispatchType(static_cast<int>(s.type),
                           CellCoord{static_cast<std::int16_t>(s.x & 0xFFFF),
                                     static_cast<std::int16_t>(
                                         (s.x >> 16) & 0xFFFF)});
        head = (head + 1) & (kBatchPlacerRingSize - 1);
        --count;
      }
      break;
    }

    // 处理 head 槽：复制 5 dword（0x5A8AEC）
    BatchPlacerSlot slot = ring[head];
    bool flag = true;           // [esp+0x13]=1
    int success = 0;            // [esp+0x2C]
    // 临时数组（operator new 的结果）。声明提到所有 goto 之前，
    // 避免 goto 跨过初始化（MSVC C2362）；原版对应分支见 0x5A8CFA。
    std::vector<BatchPlacerSlot> temp;

    if (slot.type == 0xFFFF) {
      // 0x5A8B35 je 0x5A8DD6 清理分支（空槽）：不入队，仅清理 → 出队
      // 0x5A8E25：slot[2]!=0xFFFF 时 0x5A6C10（入队空槽 slot[2] 恒 0xFFFF）
      goto dequeue;
    }

    // 0x5A8B3B：slot[2]!=0xFFFF 时 0x5A7FA0（入队槽恒 0xFFFF，不触发）
    if (slot.sentinel != 0xFFFF) {
      world.CleanType(static_cast<int>(slot.sentinel),
                      CellCoord{static_cast<std::int16_t>(slot.x2 & 0xFFFF),
                                static_cast<std::int16_t>(
                                    (slot.x2 >> 16) & 0xFFFF)});
    }
    // 0x5A8B53：0x5A6C10(ecx=type, edx=&槽[3], 栈参 -1,-1)
    world.DispatchType(static_cast<int>(slot.type),
                       CellCoord{static_cast<std::int16_t>(slot.x2 & 0xFFFF),
                                 static_cast<std::int16_t>(
                                     (slot.x2 >> 16) & 0xFFFF)});

    // LAT 三档映射（0x5A8B60）：[0xABC1D4]=0 → idx=type（golden 灌 0）
    std::int32_t t = static_cast<std::int32_t>(slot.type);
    int idx = 0;
    {
      constexpr std::int32_t kBaseA = 0;  // [0xABC1D4]
      constexpr std::int32_t kBaseB = 0;  // [0xABBEC0]
      if (t >= kBaseA && t < kBaseA + 0x65) {
        idx = static_cast<int>(t - kBaseA);
      } else if (t >= kBaseB && t < kBaseB + 8) {
        idx = static_cast<int>(t - kBaseB + 0x65);
      } else {
        idx = 0;
      }
    }

    const BatchAbeEntry& abe = world.AbeEntry(idx);
    if (abe.count <= 1) {
      goto dequeue;  // 0x5A8BAF jle 0x5A8D06：不入队
    }

    // count>1：operator new 临时数组（count - extra）项 × 0x14
    int extra = (slot.back != 0xFFFFFFFF) ? 1 : 0;  // setne cl / sub
    int n = abe.count - extra;
    if (n <= 0) goto dequeue;
    if (!world.AllocTemp()) goto dequeue;  // 0x5A8BD8 je 0x5A8CFA

    // 填临时数组（0x5A8BEB）：{0xFFFF, x'=0, 0xFFFF, x2'=0, -1}
    // ★原版只分配 n=count-extra 项，但子表循环上界=表项 count
    //   （0x5A8C2D mov ecx,[ebx]）——雪球槽 extra=1 时第 count 次循环
    //   访问分配块之后的堆垃圾（原版怪癖，越界读写）。golden 桩不读
    //   该垃圾值，PICK 序列不受影响；这里按 abe.count 项分配，多余项
    //   保持默认构造值（{0xFFFF,0,0xFFFF,0,-1}）等价模拟垃圾槽。
    temp.resize(static_cast<std::size_t>(abe.count));
    for (int i = 0; i < n; ++i) {
      temp[static_cast<std::size_t>(i)].type = 0xFFFF;
      temp[static_cast<std::size_t>(i)].x = 0;
      temp[static_cast<std::size_t>(i)].sentinel = 0xFFFF;
      temp[static_cast<std::size_t>(i)].x2 = 0;
      temp[static_cast<std::size_t>(i)].back = 0xFFFFFFFF;
    }

    // 遍历 0..count-1（0x5A8C2D mov ecx,[ebx]——上界 = 表项 count）
    std::uint32_t slot4 = slot.back;
    int loop_count = abe.count;
    for (int eax = 0; eax < loop_count; ++eax) {
      if (static_cast<std::uint32_t>(eax) == slot4) {
        continue;  // je 0x5A8DA4（golden slot4=-1，永不命中）
      }
      int kx = 0, ky = 0;
      if (eax < static_cast<int>(abe.sub.size())) {
        kx = abe.sub[static_cast<std::size_t>(eax)].first;
        ky = abe.sub[static_cast<std::size_t>(eax)].second;
      }
      // 方向索引（0x5A8C73 lea esi,[eax-4] / and esi,7）
      int dir = (ky - 4) & 7;
      // 坐标合成（0x5A8C4E-0x5A8CA0）：
      //   full = kx + slot.x（32 位）；x' = (full&0xFFFF)+方向dx
      //   y'  = (kx>>16)+(slot.x>>16)+方向dy
      std::uint32_t full = static_cast<std::uint32_t>(kx) + slot.x;
      std::int32_t tx = static_cast<std::int32_t>((full & 0xFFFF)) +
                        kDirTable[static_cast<std::uint32_t>(ky) & 7][0];
      std::int32_t ty = static_cast<std::int32_t>(
                            (static_cast<std::uint32_t>(kx) >> 16) +
                            (slot.x >> 16)) +
                        kDirTable[static_cast<std::uint32_t>(ky) & 7][1];
      CellCoord target{static_cast<std::int16_t>(tx),
                       static_cast<std::int16_t>(ty)};

      // 第一次 0x5A8F00（mode=1）（0x5A8CC3）
      if (!world.PickPlace(1, dir, target)) {
        flag = false;  // 0x5A8DC0：清标志 → 收尾不入队
        break;
      }
      // mode=1 成功（0x5A9158 出口）：桩写 *栈参2(sentinel)=0、*栈参3(x2)=0x11223344
      //   （0x5A8CA9 lea ebx,[edi+4] → 栈参2=&temp[+8]；0x5A8C55 lea ebp,[edi+8] → 栈参3=&temp[+0xC]）
      // ★写回槽位由"成功序号"决定而非 eax：0x5A8DA1 add edi,0x14 只在非跳过
      //   路径执行（0x5A8C48 je 0x5A8DA4 跳过时不前进），槽从 temp[0] 连续递增。
      temp[static_cast<std::size_t>(success)].sentinel = 0;
      temp[static_cast<std::size_t>(success)].x2 = 0x11223344;
      // 第二次 0x5A8F00（mode=4）（0x5A8CE1）
      if (world.PickPlace(4, dir, target)) {
        // mode=4 成功（0x5A9158 出口）：桩写 *栈参2(type)=0、*栈参3(x)=0x11223344、
        // *栈参4(back)=3（0x5A8CD3 lea ecx,[edi-4] → &temp[+0]；edi=&temp[+4]；
        // 0x5A8CD0 lea eax,[edi+0xC] → &temp[+0x10]）。
        // ★back=3 是雪球关键：下轮处理该槽时 extra=(back!=-1)=1 → n=count-1=8，
        //   且子表遍历 cmpeax,[esp+0x24]（eax==back==3）跳过第 4 项 → 每轮 16 次调用。
        temp[static_cast<std::size_t>(success)].type = 0;
        temp[static_cast<std::size_t>(success)].x = 0x11223344;
        temp[static_cast<std::size_t>(success)].back = 3;
      } else {
        // mode=4 失败（0x5A91A7 出口）：桩写 type=0xFFFF、x=0、back=-1，
        // 然后 0x5A8CEE 回填 temp[+0xC] = [esp+0x34]（=3，mode=1 成功时桩写
        // *栈参4=3 到 [esp+0x34]）。failure 项仍 success++，type=0xFFFF 入队后
        // 走清理分支（0x5A8DD6）。
        temp[static_cast<std::size_t>(success)].type = 0xFFFF;
        temp[static_cast<std::size_t>(success)].x = 0;
        temp[static_cast<std::size_t>(success)].x2 = 3;
        temp[static_cast<std::size_t>(success)].back = 0xFFFFFFFF;
      }
      ++success;  // 0x5A8D85 汇合后 success++（成功失败都 +1）
    }

    // 收尾 0x5A8D06：flag && success>0 → 批量入队 success 项
    if (flag && success > 0) {
      for (int i = 0; i < success; ++i) {
        if (count >= 0x400) {
          break;  // cmp [esp+0x5C],0x400 / jge 0x5A8D6E
        }
        ring[tail] = temp[static_cast<std::size_t>(i)];
        (void)world.TimeGetTime();
        tail = (tail + 1) & (kBatchPlacerRingSize - 1);
        ++count;
      }
    }
    world.FreeTemp();  // operator delete（0x5A8E3C）

    // 0x5A8DD2 清理分支（flag=0 时）：0x5A7E90 + 清理 temp + slot[2] 0x5A6C10
    if (!flag) {
      world.HandleType(static_cast<int>(slot.type),
                       CellCoord{static_cast<std::int16_t>(slot.x2 & 0xFFFF),
                                 static_cast<std::int16_t>(
                                     (slot.x2 >> 16) & 0xFFFF)});
      if (slot.sentinel != 0xFFFF) {
        world.DispatchType(static_cast<int>(slot.sentinel),
                           CellCoord{
                               static_cast<std::int16_t>(slot.x2 & 0xFFFF),
                               static_cast<std::int16_t>(
                                   (slot.x2 >> 16) & 0xFFFF)});
      }
    }

  dequeue:
    // 0x5A8E4D 出队：head+1 & 0x3FF、count--、placed++
    head = (head + 1) & (kBatchPlacerRingSize - 1);
    --count;
    ++placed;
    // 0x5A8E74 jne 0x5A8AE1：count!=0 继续处理下一槽
  }

  // ⑤ 返回 0x5A8EDF：al = (placed > 1) ? 1 : 0
  return (placed > 1) ? 1 : 0;
}

}  // namespace rmg
}  // namespace ra2