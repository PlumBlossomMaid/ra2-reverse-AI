#include "dead_code_5a3a60.h"

namespace ra2 {
namespace rmg {

// 0x5A3A60（125B，cdecl 无参 ret 0——纯工具函数）：
//   CellIterator_Reset(0x578350，thiscall ecx=0x87F7E8)
//   loop（0x5A3A78 起）：cell = CellIterator_Next(0x578290，thiscall
//   ecx=0x87F7E8，返回 cell 指针）→ test eax,eax / je 0x5A3AD9（NULL
//   终止）→ 非 NULL：
//     ecx = [eax+0x24]（cell 坐标打包 dword：低16=X、高16=Y）
//     Y = movsx word [esp+6]（高16 符号扩展）
//     idx = Y*[0x89C2DC] + X（imul 边长 + movsx cx 低16 符号扩展）
//     p = [0xABED10] + idx*0x50（lea eax,[eax+eax*4]; shl eax,4）
//     p+0x20 = [0x7ED7F8]；p+0x24 = [0x7ED7FC]
//     p+0x28 = [0x7ED7F0]；p+0x2C = [0x7ED7F4]
//   ★零取数点（全程不调 Randomizer，golden next 实证）
void DeadCode5A3A60(const DeadCode5A3A60Context& ctx) {
  int x = 0;
  int y = 0;
  while (ctx.world->NextCell(x, y)) {  // CellIterator_Next 打桩；NULL 终止
    const std::int32_t idx = y * ctx.side + x;  // imul 边长 + movsx X
    if (idx < 0 || static_cast<std::size_t>(idx) >= ctx.work_size) {
      continue;  // 防御：work 越界（原版 golden 内不会发生）
    }
    DeadCode5A3A60Context::WorkSlot& p = ctx.work[idx];
    p.f28 = ctx.c_f0;  // p+0x28 = [0x7ED7F0]
    p.f2C = ctx.c_f4;  // p+0x2C = [0x7ED7F4]
    p.f20 = ctx.c_f8;  // p+0x20 = [0x7ED7F8]
    p.f24 = ctx.c_fc;  // p+0x24 = [0x7ED7FC]
  }
}

}  // namespace rmg
}  // namespace ra2