#include "dead_code_58c5a0.h"

#include "scaled_random.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace ra2 {
namespace rmg {

// 0x58C5A0（259B；ecx=输出指针、edx=arg2、栈参1=arg3，ret 4）：
//   side  = [0x89C2DC]；total = side*side（imul）
//   scale = (double)(uint64)total       （fild qword 无符号读）
//   loop: r = Random()（0xABE890 全局）→ v = _ftol((double)(uint64)r * scale
//             * kInvUintMax)            （RC=chop 链）
//         if ((unsigned)v > total-1) goto loop      （ja 拒绝采样）
//         ★rep movsd 0x14 dword（80B）把 work[v] 复制到 work[0]（edi=ebx=
//           [0xABED10] work 基址）——判定/输出都读复制后 work[0]
//         ★尝试计数 [esp+0x10]：inc → cmp 0x64 → rep movsd → 存回 → jge
//           失败出口（0x58C685）——inc-before-cmp，第 100 次失败
//         cell = GetCellAt(0x5657A0, &work[0])（thiscall ecx=0x87F7E8）
//         if (work[0].x == 0 && work[0].y == 0) goto loop （空格子，★GetCellAt
//           先于空格判定——0x58C637 call 在 0x58C63C cmp 之前）
//         if (0x56D230(0x87F7E8, cell+0x24, 0, 0) != arg2) goto loop
//           （thiscall+3 栈参 ret 0xC；golden 打桩固定值，真身语义留后续）
//         if (0x486380(cell) == 0) goto loop         （谓词：cell+0x38==0xFFFF
//           ||==0 → al=1 通过）
//         if (arg3 != -1 && work[0].[0x38] != arg3) goto loop（arg3==-1 跳过）
//         *out = work[0] 首 dword（x | (y<<16)）；ret 4
//   fail: *out = (0,0)（随机数不回退）
std::uint32_t DeadCode58C5A0(const DeadCode58C5A0Context& ctx) {
  const std::uint32_t side = static_cast<std::uint32_t>(ctx.side);
  const std::uint64_t total = static_cast<std::uint64_t>(side) * side;
  const double scale = static_cast<double>(total);  // fild qword 无符号读

  int attempts = 0;  // 原版 [esp+0x10]：只对「通过拒绝采样的被接受采样」计数
  for (;;) {
    const std::uint32_t r = ctx.rng->Random();

#if defined(_M_X64) || defined(__x86_64__)
    const unsigned csr = _mm_getcsr();
    _mm_setcsr((csr & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO);
#endif
    volatile double t = static_cast<double>(static_cast<std::uint64_t>(r)) * scale;
    volatile double t2 = t * kInvUintMax;
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(csr);
#endif

    const std::int32_t v = Ftol(t2);  // _ftol（向零截断，FPCW=0x0E7F）
    if (static_cast<std::uint32_t>(v) > static_cast<std::uint32_t>(total - 1)) {
      continue;  // ja 无符号拒绝采样（★不计数——回 0x58C5E3 只重取随机数）
    }
    if (v < 0 || static_cast<std::uint32_t>(v) >= ctx.work.size()) {
      continue;  // 防御：work 越界（原版 golden 内不会发生）
    }
    // ★rep movsd 复制 work[v] → work[0]（80B）；C++ 侧直接读 work[v] 等价
    const DeadCode58C5A0Rec& p = ctx.work[static_cast<std::uint32_t>(v)];

    // ★inc（0x58C623）→ cmp 0x64 → rep movsd → 存回 → jge 失败出口：
    //   第 100 次被接受的采样即失败，共消耗 100 次 Random（golden next 校验）。
    if (++attempts >= 100) {
      return 0;  // 失败出口 *out=(0,0)，不再判定、不额外采样
    }

    const int x = static_cast<int>(p.coord & 0xFFFF);
    const int y = static_cast<int>((p.coord >> 16) & 0xFFFF);
    // GetCellAt(&work[0]) 先执行（内部不消耗 RNG），返回值后续两条谓词用
    // ——C++ 侧把 coor→cell 封装进 world 回调，顺序无观测差异。
    if (x == 0 && y == 0) {
      continue;  // 空格子 (0,0) 重抽（0x58C63C-0x58C647）
    }

    // 0x56D230(0x87F7E8, cell+0x24, 0, 0) 返回值 != arg2 → 重抽
    if (ctx.world->D56Value(x, y) != ctx.arg2) {
      continue;
    }
    // 0x486380(cell)：cell+0x38==0xFFFF||==0 通过；al==0 → 重抽
    if (!ctx.world->CellPass(x, y)) {
      continue;
    }
    // arg3 == -1 → 跳过 [0x38] 比对（直达成功出口 0x58C6A3）
    if (ctx.arg3 != -1 && p.field38 != static_cast<std::uint32_t>(ctx.arg3)) {
      continue;
    }
    return p.coord;  // x | (y<<16)
  }
}

}  // namespace rmg
}  // namespace ra2