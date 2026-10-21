// 死代码复现 —— 0x5A5360 废弃方案最大函数（1913B，thiscall + 2 栈参 ret 8）
// （batch 1234）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A5360（0x778=1912B 主体，
// 复杂路径 ret 8 @0x5A5A9C / 简单路径 ret 8 @0x5A5AD6；0x5AD8 是同名简写
// 指 0x5A5AD8，勿与独立子函数 sift-down 0x5AD870 混淆）。
// 调用约定：thiscall ecx=this（未用）+ 2 栈参：
//   [esp+0x64] = 起始点结构指针 {u16 X, u16 Y}（打包 dword 低16=X 高16=Y）
//   [esp+0x68] = N 点数（正整数）
// 零引用死代码（废弃方案最大函数——三连 operator new + 最小堆 + 8 方向扩散 +
// bbox 双分支）。
//
// 语义（两段反汇编 0x5A5360-0x5A5AD8 逐条拼合 + golden 12 行实证，
// deadcode_5a5360_golden.txt）：
//   ① edi = N*10（lea×5 + shl 1）
//   ② operator new(N*10*8) → ebx 主 buffer（每安置点 8B = {u32 打包xy, float key}）
//     ★push 0x14 后 mov [esp+0x34],ebx 实写 push 前 [esp+0x30]（batch 1232
//     push-1 槽错位同族——原版用 push 调栈索引拼槽位）
//   ③ operator new(0x14) → 堆对象 esi：+0=count +4=容量N*10 +8=指针数组
//     （operator new(N*10*4+4)，清 N*10+1 项 0）+0xC=max节点 +0x10=min节点
//   ④ CellIterator 双轮（0x578350 Reset + 0x578290 Next 循环）：
//     work[(Y*side+X)*0x50+0x3C]=0——★探针打桩 Next 返 0 → 跳过（探针预灌
//     挡墙等效，golden 同源）
//   ⑤ work[0]=*参数1（打包 xy）、work[0]+4=0；work[(startY*side+startX)*0x50
//     +0x3C]=1；[esp+0x1C]=1（安置计数）；堆插入起始点（key=0）
//   ⑥ 出堆：buffer[1] 取出 → ebp=节点；count--；call 0x5AD870（sift-down 真身）
//   ⑦ operator new(0x18) → 向量 edi；call 0x42FCB0(edi,0,0)（真身：+4=0 +8=0
//     +0xC=1 +0xD=0 [esi]=0x7E38D0）→ [edi]=0x7E3890（覆盖虚表）+0x14=0xA
//     +0x10=0 → [edi+0x14]=N+2（容量目标）
//   ⑧ 主循环（[esp+0x18]<N 且当前节点非空）：
//     GetCellAt(ebp)（0x5657A0，探针打桩返 CELL_OBJ）→ cell+0x38=[0xABC2B0]
//     → 向量扩容/追加：count>=容量时若 [edi+0xD]!=0 或 容量==0 → call 虚表
//     槽 2（★0x7E3890 槽 2 = 0x42F860，非 0x42F970！aux 实测 12 槽：
//     [0]=0x42FBF0 [1]=0x42F620 [2]=0x42F860...；探针打桩扩容到栈参新容量
//     + 固定 buffer）→ al!=0 才追加 buf[count]=*ebp，count++
//     → 8 方向扩散（dir=[esp+0x20] 0..7）：
//       写点 = 主buffer + [esp+0x1C]*8
//       dir&7 → lea ecx,[dir*4+0x89F688] → call 0x42D510（真身：
//       out = 输入坐标 + 表项{dx:16,dy:16}，ret 8）
//       → 菱形边界（A=W=[0xABED04] B=W+2H=[0xABED08]）：
//         X+Y<=A 或 X-Y>=A 或 Y-X>=A 或 X+Y>B → skip
//       → GetCellAt(&out)（打桩）→ 0x486380（打桩返 1）→ cell+0x44!=-1 → skip
//       → work[idx*0x50+0x3C]==1（已访问）→ skip；+0x45!=0 → skip
//       → 成功：写点=out 打包xy；key = max(|startX-curX|,|startY-curY|) +
//         (uint32)Random()*[0x7ED818]=3.0*kInvUintMax[0x7ED898]（fild qword
//         无符号 + 两次 fmul + fild dist + faddp——★RNG#1 每成功一次消耗 1）
//       → work[idx*0x50+0x3C]=1；[esp+0x1C]++；堆插入（key 比较 fcomp）
//     → 出堆下一节点；[esp+0x18]++（处理数）
//   ⑨ 清理：delete(主buffer)；delete(堆指针数组);delete(堆)；call 0x5A7640
//     （bbox 真身：edx=向量 → 遍历元素算 min/max → 输出 rect{minX,minY,
//     W=maxX-minX+1,H=maxY-minY+1}）
//   ⑩ 双分支分界：处理数 cmp 100：
//     jle → 简单路径；>100：fild(W*H) vs fild(count)*1.6 → fcompp →
//     test ah,0x41 → jne 简单（★复杂路径条件 = count×1.6 > W×H，点密集）
//     复杂：外循环 ebp=count-1..0 × 内层 dir 0..7：取 vec[ebp]+偏移 → 菱形
//     边界 → GetCellAt → cell+0x38==[0xABC2B0] → skip（探针打桩全等 →
//     向量不收缩）；否则删除 vec[ebp]（[edi+0x10]--、后续前移）→ 收缩后
//     遍历标记 work[idx*0x50+0x45]=1 → count/3（magic 0x55555556 有符号
//     除法）次放置循环：
//       [esp+0x50]=count double → RNG#2（0x65C780）→ fild qword → fmul count
//       → fmul kInv → _ftol → ja eax>count-1 重抽 → v=vec[eax]
//       （★PickBelow(count) 形态）→ RNG#3 → fmul [0x7EDAF8]=1.63e-9 → _ftol
//       → ja >6 重抽 → t∈[0,6] → t<=3 直接放置；t>3：2×2 邻域检查
//       （ebp,esi∈{0,1}）：work[(y+esi)*side+(x+ebp)].+0x45!=0 时 GetCellAt
//       → cell+0x38==[0xABC2B0] 保留 flag 否则 flag=0 → flag 通过 →
//       eax=[0xAA10A4]+t；push -1;-1; lea edx,[esp+0x50]; call 0x5A6C10
//       （★打桩 ret 8 记录）→ [esp+0x34]-- 循环 count/3 次
//     简单：遍历 vec：GetCellAt(&vec[i]) → cell+0x38=0xFFFF；edi!=0 →
//       call 虚表槽 0（0x42FBF0 打桩，push 1）→ 返回 eax=0
//   ★随机消耗：扩散每成功 1 次 = RNG#1；复杂路径每放置 = RNG#2（+重抽）+
//     RNG#3（+重抽）。简单路径零 RNG。
//   ★原版怪癖（照抄）：push 槽错位（0x5A5395 写 push 前 [esp+0x30]）；
//     work 越界（菱形边界允许坐标到 ~2side，但 work 数组仅 side² 项——原版
//     死代码越界读堆=UB 从未运行，探针确定化模拟 side² 外 +0x3C=1 挡墙）；
//     fcompp test ah,0x41（C0|C3）分支判定；magic 除法 count/3；虚表槽
//     2 扩容 + 槽 0 析构调用现场。
#ifndef RA2_RMG_DEAD_CODE_5A5360_H_
#define RA2_RMG_DEAD_CODE_5A5360_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

class Randomizer;

// 这一阶段要问外界的事（work/cell/方向表/堆插入键/放置全部经世界回调注入）：
class DeadCode5A5360World {
 public:
  virtual ~DeadCode5A5360World() {}

  // work 数组（[0xABED10]，扩展区 ≥(4*side*side+4*side) 项 × 0x50B）——
  //   idx = Y*side+X：+0x3C 已访问标志（dword）、+0x45 区间标记（byte）
  virtual std::uint32_t WorkVisit(std::uint32_t idx) = 0;   // +0x3C dword
  virtual void SetWorkVisit(std::uint32_t idx) = 0;         // +0x3C = 1
  virtual bool WorkFlag45(std::uint32_t idx) = 0;           // +0x45 byte != 0
  virtual void SetWorkFlag45(std::uint32_t idx) = 0;        // +0x45 = 1

  // GetCellAt(0x5657A0) 打桩：返固定 CELL_OBJ（+0x38 可写 +0x44=-1）；
  //   set 版本给主循环写 cell+0x38=[0xABC2B0]、简单路径写 0xFFFF
  virtual void SetCell38(std::uint32_t xy, std::uint32_t v) = 0;
  virtual std::uint32_t GetCell38(std::uint32_t xy) = 0;    // 复杂路径读
  virtual bool Cell44Neg1(std::uint32_t xy) = 0;            // cell+0x44 == -1

  // 0x486380 谓词（探针打桩返 1）
  virtual bool Pred486(std::uint32_t xy) = 0;

  // 8 方向表（[0x89F688]，每项 {dx:int16, dy:int16} 打包 dword 低16=dx 高16=dy）
  virtual std::uint32_t DirOffset(int dir) = 0;

  // CellIterator（0x578350/0x578290）打桩：空迭代器（false=结束，跳过清 work）
  virtual bool IterNext() = 0;

  // 虚表槽 2（0x42F860 向量扩容，thiscall+2 栈参 ret 8）——
  //   探针打桩：扩容到 newcap、buf=固定 VEC_BUF、返 al=1
  virtual bool VecGrow(std::uint32_t newcap) = 0;
  // 虚表槽 0（0x42FBF0 析构/清空，thiscall+1 栈参 ret 4，探针打桩）
  virtual void VecClear() = 0;

  // 0x5A6C10 放置（thiscall+2 栈参 ret 8；ecx=[0xAA10A4]+t，探针打桩记录）
  virtual void Place(std::int32_t t) = 0;
};

struct DeadCode5A5360Context {
  DeadCode5A5360World* world = nullptr;
  Randomizer* rng = nullptr;
  std::int32_t start_x = 0;   // 起始点 X（u16，打包低 16 位）
  std::int32_t start_y = 0;   // 起始点 Y（u16，打包高 16 位）
  std::int32_t n = 0;         // 参数 2 点数
  std::int32_t side = 0;      // [0x89C2DC] MapGen 边长
  std::int32_t bound_a = 0;   // [0xABED04] 菱形边界 A = MapRect.W（方阵=side）
  std::int32_t bound_b = 0;   // [0xABED08] 菱形边界 B = W + 2H（方阵=3*side）
  std::uint32_t abc2b0 = 0;   // [0xABC2B0] cell+0x38 哨兵源
  // 输出（golden 对拍）：子调用计数（world 回调打桩记录）
  std::int32_t calls_new = 0, calls_del = 0;
  std::int32_t calls_iter = 0;
  std::int32_t calls_gcell = 0, calls_p486 = 0;
  std::int32_t calls_slot8 = 0, calls_slot0 = 0, calls_6c10 = 0;
};

// 0x5A5360。零引用死代码，语义按黄金逐条复刻。
// 随机消耗：扩散每成功 1 次 RNG#1（key 扰动）；复杂路径每放置 RNG#2+
// RNG#3（含拒绝重抽）。简单路径零 RNG（golden next 对拍消耗次数与路径）。
void DeadCode5A5360(DeadCode5A5360Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A5360_H_