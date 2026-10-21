// 死代码复现 —— 0x5A80B0 编排树根 B（546B，死代码树第二根）
// （batch 1233）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A80B0（546B）
// 调用约定：thiscall ecx=this ret 0——读 this->[0x60]（有符号 int this60）。
// 零引用死代码（编排树根 B——与根 A 0x5A5020 同构、互不相调，各串一棵
// 子树：同一死代码方案的纵向切分）。
//
// 语义（反汇编全 0x222=546B 拼读 + golden 12 行实证，
// deadcode_5a80b0_golden.txt）：
//   A. 数组初始化两循环（与根 A 相同）：0xABE378 起 12B 步长写
//      [0xABC1D4]+i（101 项到 0xABE834）；0xABE834 起写 [0xABBEC0]+i
//      （8 项到 0xABE894）
//   B. [esp+0x30] = _ftol((double)this60 + 0.49) * 2
//      （fild dword [this+0x60] → fadd [0x7EDB10]=0.49 → fadd st0,st0
//      = ×2 → _ftol——★根 A 是 (this60+0.32)*3.0）
//   C. 外层循环（[esp+0x2C] 轮次<10 且 [esp+0x18] 内层完成数<上限）：
//      内层（0x5A8133 起）：取数 PickBelowMul(side²) 形态——v=_ftol(
//      (double)(uint32)Random() * side² * kInvUintMax)，拒绝条件
//      ja 无符号 v > side²-1（★根 A 用 PickRange(0, side²-1)）→
//      work[v].xy==(0,0) 判空重抽（0x42D470 构造+0x50E470 判等）→
//      成功点计数 [esp+0x14]++ → ebp>200 强制 (0,0) → GetCellAt→
//      0x486380 谓词失败重选（ebp 保留）→ [esp+0x14]<20 时：
//        work[y*side+x].+0x45 != 0 → 重选（ebp 清零）
//        == 0 → 菱形校验 0x5A7250(x-4, y-4, w=8, h=8, flagA=0, flagB=0)
//          ★push ebx 位移：lea ecx,[esp+0x50] 在 push 后执行，实际指
//          push 前 [esp+0x4C]——rect{+0=X-4/+4=Y-4/+8=8/+0xC=8} 对位
//          （与根 A 的 push-1 槽错位同族）
//        → 失败重选（ebp 清零）；通过 → 0x578460 IsWithinUsableArea
//          (&coord, 1)（thiscall ecx=0x87F7E8 + 2 栈参 ret 8）失败重选
//   D. 成功点==20 → 跳过三连调；否则**无条件三连调**（★无概率判定！
//      根 A 有 Random 概率门 1.0-this60 vs r·kInv 分叉三连调/magic，
//      根 B 直接三连调）：
//      al = 0x5A89D0(&coord)（al!=0 继续）→ 0x5A82E0 → 0x5A8720 →
//      inc [esp+0x18]（内层完成数）→ 外层计数++（0x5A82B8 无条件）
//  ★随机消耗：PickBelowMul 每次 1 Random + 空格/谓词失败重抽
//    （golden next 对拍消耗次数与路径）
//  ★打桩记录（golden 聚焦调用序列与 next）：
//    0x5A7250 → 返 1（菱形通过；ret 4——现场 1 栈参）
//    0x578460 → 返 1（可用区通过；ret 8——现场 2 栈参）
//    0x5A89D0 → 返 1（al!=0 继续三连调）；0x5A82E0/0x5A8720 → ret 4
//    0x486380 谓词真身（cell tile=0 → 通过）
#ifndef RA2_RMG_DEAD_CODE_5A80B0_H_
#define RA2_RMG_DEAD_CODE_5A80B0_H_

#include <array>
#include <cstdint>

namespace ra2 {
namespace rmg {

class Randomizer;

// 这一阶段要问外界的事（work/谓词/菱形/可用区/三连调全部经世界回调注入）：
class DeadCode5A80B0World {
 public:
  virtual ~DeadCode5A80B0World() {}

  // work 数组（[0xABED10]，side² 项 × 0x50B）——
  //   v 选点后：坐标打包 dword（低16=X 高16=Y）
  //   idx = Y*side+X：+0x45 保护区字节（非 0 重选）
  virtual std::uint32_t WorkCoord(std::uint32_t v) = 0;
  virtual bool WorkEmpty(std::uint32_t v) = 0;      // work[v].xy==(0,0) 空格
  virtual bool WorkFlag45(std::uint32_t idx) = 0;   // work[idx].+0x45 != 0

  // GetCellAt(0x5657A0) → 0x486380 谓词（cell+0x38==0xFFFF||==0 通过）
  virtual bool CellPass(int x, int y) = 0;

  // 0x5A7250 菱形域矩形校验（x-4, y-4, w=8, h=8, flagA=0, flagB=0——
  // 已复刻 DeadCode7250 语义；本批打桩返 1=通过）
  virtual bool DiamondPass(int x, int y) = 0;

  // 0x578460 IsWithinUsableArea(&coord, 1)（thiscall ecx=0x87F7E8
  // + 2 栈参 ret 8；本批打桩返 1=通过）
  virtual bool UsablePass(int x, int y) = 0;

  // —— 三连调（thiscall ecx=this + 1 栈参 &coord ret 4）——
  // 0x5A89D0 返回 al（al==0 中止三连调）
  virtual bool Sub5A89D0() = 0;
  virtual void Sub5A82E0() = 0;
  virtual void Sub5A8720() = 0;
};

struct DeadCode5A80B0Context {
  DeadCode5A80B0World* world = nullptr;
  Randomizer* rng = nullptr;
  std::int32_t this60 = 0;   // this->[0x60]（fild dword）
  std::int32_t side = 0;     // [0x89C2DC] MapGen 边长
  std::uint32_t src1 = 0;    // [0xABC1D4] 数组初始化源 1
  std::uint32_t src2 = 0;    // [0xABBEC0] 数组初始化源 2
  // 输出：数组初始化两段（12B 步长写首 dword）——101 项 + 8 项
  std::array<std::uint32_t, 101> arr1 = {};  // [0xABE378+i*12] = src1+i
  std::array<std::uint32_t, 8> arr2 = {};    // [0xABE834+i*12] = src2+i
};

// 0x5A80B0。零引用死代码，语义按黄金逐条复刻。子函数打桩记录调用序列。
// 随机消耗：PickBelowMul(side²) 每次 1 Random + 空格/谓词失败重抽
// （golden next 对拍消耗次数与路径）。
// ctx 非 const：arr1/arr2 是输出（数组初始化副作用——原版写全局内存）。
void DeadCode5A80B0(DeadCode5A80B0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A80B0_H_