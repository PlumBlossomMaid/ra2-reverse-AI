// 死代码复现 —— 0x5A1E10 MapType 3/4 门禁编排（56B，零取数点）
// （batch 1230）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A1E10（56B）
// 调用约定：thiscall 无栈参 ret 0——ecx=this（mov esi,ecx），读
// this->[0x3C]（dword）。
// 零引用死代码（TSV:1001 已 semantics 登记，batch 319；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 16 行实证，deadcode_5a1e10_golden.txt）：
//   if (this->[0x3C] == 4 || this->[0x3C] == 3) {
//       0x58EBC0();                     // 无参 ret 0（ecx 残留无契约）
//       0x58EF10();                     // 无参 ret 0
//       0x5A19E0(this);                 // thiscall ecx=this，ret 0
//       RepaintAll(0x578E60, 0x87F7E8, 0, -1);
//         // thiscall ecx=Instance +2 栈参 ret 8
//         // 实参形态：push -1; push 0; mov ecx,0x87F7E8; call
//         // → 参数1=0（[esp+4]）、参数2=-1（[esp+8]）
//       0x5A17F0(this);                 // thiscall ecx=this，ret 0
//   }
//   else 直接返回（跳过全部调用）
//
// ★与 0x58C4A0 族的差异：**编排门禁**——不是取数循环，是五个子调用的
// 固定顺序编排（死代码区「另一套 MapType 3/4 方案」的总入口之一）。
// ★零取数点：门禁本身不调 Randomizer（五个子调用本批全部打桩，
// golden next 字段实证：同种子所有用例 next 相同 = 调用前后 RNG 状态不变）。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① [this+0x3C] == 4 || == 3 门禁（双值判断）；
//   ② 五连调固定顺序（0x58EBC0 → 0x58EF10 → 0x5A19E0 → RepaintAll
//      → 0x5A17F0）；
//   ③ RepaintAll 实参形态 push -1; push 0（参数1=0、参数2=-1——
//      owner=-1 与活代码 0x598D73 同款）；
//   ④ 门禁不命中时零调用直接返回（else 分支无任何副作用）。
#ifndef RA2_RMG_DEAD_CODE_5A1E10_H_
#define RA2_RMG_DEAD_CODE_5A1E10_H_

#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（五个子调用全部经世界回调注入——
// 本批 golden 打桩记录调用序列，C++ 侧复刻「顺序」而非子函数本体）：
class DeadCode5A1E10World {
 public:
  virtual ~DeadCode5A1E10World() {}

  // 0x58EBC0（无参 ret 0——门禁编排第一步）
  virtual void Sub58EBC0() = 0;

  // 0x58EF10（无参 ret 0——门禁编排第二步）
  virtual void Sub58EF10() = 0;

  // 0x5A19E0（thiscall ecx=this，ret 0——门禁编排第三步）
  virtual void Sub5A19E0() = 0;

  // 0x578E60 RepaintAll（thiscall ecx=0x87F7E8 Instance +2 栈参 ret 8；
  // 实参 参数1=0、参数2=-1——owner=-1 活代码同款）
  virtual void RepaintAll(int arg1, int arg2) = 0;

  // 0x5A17F0（thiscall ecx=this，ret 0——门禁编排第五步）
  virtual void Sub5A17F0() = 0;
};

struct DeadCode5A1E10Context {
  DeadCode5A1E10World* world = nullptr;
  std::uint32_t maptype = 0;  // this->[0x3C]（==4 || ==3 才执行编排）
};

// 0x5A1E10。零引用死代码，语义按黄金逐条复刻。零取数点（不调 RNG）。
// 门禁命中时按固定顺序回调五个子调用；不命中时零回调。
void DeadCode5A1E10(const DeadCode5A1E10Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A1E10_H_