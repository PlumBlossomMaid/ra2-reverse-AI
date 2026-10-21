#include "dead_code_5a6ba0.h"

namespace ra2 {
namespace rmg {

// 0x5A6BA0（110B，thiscall 无栈参 ret 0——ecx=锚点 CellStruct）：
//   X = movsx word [ecx]；Y = movsx word [ecx+2]
//   loop：x ∈ [X-7, X+7) 外层（lea edi,[ecx-7] 起，cmp edi,edx / jge
//         退出返 1——X 边界先判）
//         y ∈ [Y-7, Y+7) 内层（lea ebp,[eax-7] 起，cmp ebp,ebx / jge
//         跳外层——Y 边界先判）
//         每格：{x,y} → GetCellAt(0x5657A0，thiscall ecx=0x87F7E8，
//         1 栈参=坐标指针 ret 4) → cell → 0x4863D0(cell)（cell+0x38
//         落入多段阈值区间；本批打桩 cell+0x38==k）→ 谓词真 → 返 0
//   全邻域无命中 → 返 1（al 返回值：0=邻域内有可用格/1=全不可用）
//   ★零取数点（全程不调 Randomizer，golden next 字段实证）
int DeadCode5A6BA0(const DeadCode5A6BA0Context& ctx) {
  const int X = ctx.x;
  const int Y = ctx.y;
  for (int x = X - 7; x < X + 7; ++x) {   // 外层 X（0x5A6BB1 lea edi,[ecx-7]）
    for (int y = Y - 7; y < Y + 7; ++y) { // 内层 Y（0x5A6BB6 lea ebp,[eax-7]）
      if (ctx.world->CellPass(x, y)) {
        return 0;  // 邻域内有可用格（0x5A6BEA jne 0x5A6C04 → xor al,al）
      }
    }
  }
  return 1;  // 邻域全不可用（0x5A6BFA-0x5A6BFD → mov al,1）
}

}  // namespace rmg
}  // namespace ra2