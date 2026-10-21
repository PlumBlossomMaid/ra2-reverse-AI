// 死代码复现 —— 0x5A5020 编排树根 A（827B，死代码树第一根）
// （batch 1232）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A5020（827B）
// 调用约定：thiscall ecx=this ret 0——读 this->[0x60]（有符号 int this60）。
// 零引用死代码（编排树根 A——分支目标 0x5A82E0/0x5A89D0/0x5A8720/
// 0x5A5360/0x5A5B50/0x5A6510/0x5A6920/0x5A78D0 均为后批次目标）。
//
// 语义（反汇编全 0x33B=827B 四段拼读 + 诊断槽值实证 + golden 12 行，
// deadcode_5a5020_golden.txt）：
//   A. 数组初始化两循环：
//       0xABE378 起 12B 步长写 dword = [0xABC1D4]+i（101 项到 0xABE834）
//       0xABE834 起 12B 步长写 dword = [0xABBEC0]+i（8 项到 0xABE894）
//   B. [esp+0x40] = _ftol((double)this60+0.32)*3.0（fild dword → fadd
//      0.32 → fmul 3.0 → _ftol）——内层完成数上限（golden: 1→3 轮、
//      2→6 轮、3→9 轮）
//   C. 外层循环（[esp+0x24] 轮次<10；[esp+0x18] 内层完成数<上限）：
//       内层（0x5A50B1）：v=PickRange(0x598030,0,side²-1) →
//       work[v].xy==(0,0) 判空重抽（0x42D470 构造+0x50E470 判等）→
//       成功点计数 [esp+0x20]（上限 20）→ ebp>200 强制 (0,0) →
//       GetCellAt(0x5657A0)→0x486380 谓词失败重选 → [esp+0x20]<20 时
//       work[y*side+x].+0x45!=0 走 ±7 邻域扫描（GetCellAt→0x4863D0，
//       命中→ebp=0 重抽）→ 概率判定（0x5A51C6）
//   D. 概率判定：r=Random()（fild qword 无符号→*kInvUintMax）vs
//      1.0-(double)this60（fsubr 1.0 后 fcompp；test ah,0x41=C0|C3）：
//       ST>ST1（1.0-this60 > r*kInv）→ 三连调路径；否则 magic 路径。
//       ★整数 this60≥1 时 1-this60≤0 恒 ≤ r*kInv∈[0,1) → **三连调
//       不可达**（C++ 照抄两分支，golden 只覆盖 magic 路径——实锤）
//   E. magic 路径（0x5A5238）：[esp+0x20]==20 → 跳过五连调；
//      magic 除法定标（0x88888889×(H*W*2) 高位+修正，W=H=100→1916）
//      → (magic-300) vs 2000.0 / (magic+300) vs 100.0 双段选
//      scale/offset（段1：300.0/magic；段2 0x5A52AB：0x408DB000/
//      0x40906800）→ NormalDeviate 拒绝循环（接受 [100,2000]）→
//      _ftol → h → 0x5A5360(&coord,h)（打桩返非 0）→ 五连调
//      0x5A5B50/0x5A6510/0x5A6920/0x5A78D0 → 虚函数调用
//      ★0x5A5333 mov ecx,[esp+0x18]; inc ecx; push 1;
//      mov [esp+0x1c],ecx——push 后 [esp+0x1c]==push 前 [esp+0x18]！
//      "写错槽"实为内层完成数+1（golden 3/6/9 轮退出机制）
//   F. 三连调路径（0x5A51F8，不可达但照抄）：al=0x5A89D0(&coord)；
//      al 非 0 → 0x5A82E0 → 0x5A8720 → inc [esp+0x18]（不动 [esp+0x24]）
//
// ★随机消耗非定值：PickRange 空格重抽 + 概率判定 Random 无条件 1 次
//   （golden next 对拍消耗次数与路径）
// ★打桩记录（golden 聚焦调用序列与 next）：
//   0x4863D0 → 恒 0（邻域扫描穿透）；0x5980C0 → 0.2（拒绝窗口
//   [100,2000] 一次通过）；0x5A5360 → 返回非 0（五连调继续）；
//   虚函数调用三级指针（esi→[esi]=vtable→[[esi]]=桩体）
#ifndef RA2_RMG_DEAD_CODE_5A5020_H_
#define RA2_RMG_DEAD_CODE_5A5020_H_

#include <array>
#include <cstdint>

namespace ra2 {
namespace rmg {

class Randomizer;

// 这一阶段要问外界的事（work/谓词/子函数打桩全部经世界回调注入）：
class DeadCode5A5020World {
 public:
  virtual ~DeadCode5A5020World() {}

  // work 数组（[0xABED10]，side² 项 × 0x50B）——
  //   v 选点后：坐标打包 dword（低16=X 高16=Y）
  //   idx = Y*side+X：+0x45 保护区字节（非 0 走邻域扫描）
  virtual std::uint32_t WorkCoord(std::uint32_t v) = 0;
  virtual bool WorkEmpty(std::uint32_t v) = 0;      // work[v].xy==(0,0) 空格
  virtual bool WorkFlag45(std::uint32_t idx) = 0;   // work[idx].+0x45 != 0

  // GetCellAt(0x5657A0) → 0x486380 谓词（cell+0x38==0xFFFF||==0 通过）
  virtual bool CellPass(int x, int y) = 0;

  // 0x4863D0 邻域谓词（本批打桩恒 false——扫描穿透到概率判定）
  virtual bool NeighborPass(int x, int y) = 0;

  // 0x5980C0 NormalDeviate（打桩返 0.2——接受窗口 [100,2000] 一次通过）
  virtual double NormalDeviate() = 0;

  // —— 子函数打桩（记录调用序列）——
  // 三连调：0x5A89D0 返回 al（al==0 中止三连调）
  virtual bool Sub5A89D0() = 0;
  virtual void Sub5A82E0() = 0;
  virtual void Sub5A8720() = 0;
  // 五连调：0x5A5360 返回对象非 0 才继续
  virtual bool Sub5A5360() = 0;
  virtual void Sub5A5B50() = 0;
  virtual void Sub5A6510() = 0;
  virtual void Sub5A6920() = 0;
  virtual void Sub5A78D0() = 0;
  // 虚函数调用（0x5A5337 mov edx,[esi]; 0x5A5342 call [edx]，push 1）
  virtual void VfnCall() = 0;
};

struct DeadCode5A5020Context {
  DeadCode5A5020World* world = nullptr;
  Randomizer* rng = nullptr;
  std::int32_t this60 = 0;   // this->[0x60]（fild dword）
  std::int32_t side = 0;     // [0x89C2DC] MapGen 边长
  std::int32_t map_w = 0;    // [0x87F8DC] magic 除法输入
  std::int32_t map_h = 0;    // [0x87F8E0] magic 除法输入
  std::uint32_t src1 = 0;    // [0xABC1D4] 数组初始化源 1
  std::uint32_t src2 = 0;    // [0xABBEC0] 数组初始化源 2
  // 输出：数组初始化两段（12B 步长写首 dword）——101 项 + 8 项
  std::array<std::uint32_t, 101> arr1 = {};  // [0xABE378+i*12] = src1+i
  std::array<std::uint32_t, 8> arr2 = {};    // [0xABE834+i*12] = src2+i
};

// 0x5A5020。零引用死代码，语义按黄金逐条复刻。子函数打桩记录调用序列。
// 随机消耗：PickRange（空格重抽）+ 概率判定 Random 无条件 1 次每轮
// （NormalDeviate 打桩零消耗）；next 对拍验证消耗次数与路径。
// ctx 非 const：arr1/arr2 是输出（数组初始化副作用——原版写全局内存）。
void DeadCode5A5020(DeadCode5A5020Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A5020_H_