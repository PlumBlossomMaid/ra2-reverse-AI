#include "dead_code_5a1e10.h"

namespace ra2 {
namespace rmg {

// 0x5A1E10（56B，thiscall 无栈参 ret 0——ecx=this）：
//   mov esi,ecx → eax = [esi+0x3C] → cmp eax,4 / je 0x5A1E20 →
//   cmp eax,3 / jne 0x5A1E46（不命中直接返回）
//   命中（==4 || ==3）按固定顺序五连调：
//     0x58EBC0()            （无参 ret 0）
//     0x58EF10()            （无参 ret 0）
//     0x5A19E0(this)        （thiscall ecx=this，ret 0）
//     RepaintAll(0x578E60, 0x87F7E8, 0, -1)
//       （thiscall ecx=Instance +2 栈参 ret 8；push -1; push 0;
//        mov ecx,0x87F7E8; call → 参数1=0、参数2=-1）
//     0x5A17F0(this)        （thiscall ecx=this，ret 0）
//   ★零取数点（门禁本身不调 RNG；五个子调用本批打桩，golden next 实证）
void DeadCode5A1E10(const DeadCode5A1E10Context& ctx) {
  if (ctx.maptype == 4 || ctx.maptype == 3) {
    ctx.world->Sub58EBC0();
    ctx.world->Sub58EF10();
    ctx.world->Sub5A19E0();
    ctx.world->RepaintAll(0, -1);  // 实参形态 push -1; push 0
    ctx.world->Sub5A17F0();
  }
  // else：零调用直接返回（0x5A1E46）
}

}  // namespace rmg
}  // namespace ra2