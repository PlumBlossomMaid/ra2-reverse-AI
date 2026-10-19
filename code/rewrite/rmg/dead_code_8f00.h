// 死代码复现 —— 0x5A8F00 正态抖动取数（batch 929）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A8F00
// 调用约定：__fastcall，ecx = A（int），edx = B（CellStruct*，读两个 word 做
//   基准坐标 bx/by），4 个栈参（mode + 三个 out 指针）ret 0x10。
//
// 零引用死代码：整个函数从未被任何活代码调用（RMG 流水线 12 阶段不经过它），
// 但内部 NormalDeviate（0x5980C0，fyl2x）在 Unicorn 不可信——golden 侧给
// 它打桩（ND_STUB 序列注入），C++ 侧按**同一桩语义**用 NormalDeviateSeq 抽象。
//
// 语义链（黄金实证 + 完整汇编逐段确认，deadcode_8f00_golden.txt，8 用例）：
//
//   [esp+4] = mode（int）        [esp+8]  = out2 指针（表族 id / 0xFFFF）
//   [esp+0xC] = out3 指针（坐标差 dword） [esp+0x10] = out4 指针（命中表项索引 / -1）
//   ecx = A（int，表族 id）        edx = B（CellStruct* = {int16 bx, int16 by}）
//
//   ① mode <= 1 捷径（0x5A8F19）：edi = 1（采样值固定为 1）
//   ② 正态段（mode > 1）：fild 栈参1=param1=A →
//       if A < [0x7E48F0]=1.5：mid = A - 2.0，span = mid * 0.5
//       else：span = 0.5，mid = 2.0
//       → call 0x5980C0（NormalDeviate 桩）→ v = ND*span + mid
//   ③ 双向拒绝（0x5A8F62，test ah,1 / 0x41）：
//       if v < 2.0 或 v > mode → 重新采样（n++ 后重试）
//   ④ _ftol（RC=chop）→ n（int）
//   ⑤ eax = (A + 4*n) * 4 选双表：
//       [0x82B530] 表1 = P 指针数组（每 idx 一个 id 数组）
//       [0x82B5B0] 表2 = 条目数 ebp（[esp+0x28]）
//       n == 2 走 0x5A8FDD 特殊采样（见怪癖①）
//   ⑥ 查找循环（0x5A9030）：ebx = 0..ebp-1：
//       edx = (ebx + 采样) % ebp → P[edx] = [0xABE370] 12B 项索引
//       → count <= 0 跳过（怪癖②：0x5A7D00 计数<=0 恒返 1，但此处 count 是
//         [0xABE370+idx*12] 的 count 字段，golden 灌 0 时跳过）
//       → 子表 8B/项（坐标对 dword + 域 dword）找 域 == A
//         → 命中 esi → 坐标差 = B - 子表坐标
//         → call 0x5A7D00 判定（怪癖②：恒返 1）
//         → 成功出口：*out2 = [表项+8]（表族 id），*out3 = 坐标差 dword，
//           *out4 = esi，ret 1
//   ⑦ 失败重试（0x5A90C8）：n' = (n+1) % (mode+1)（余 0 则 1），
//       n' == n 写 0xFFFF/0/-1，ret 0
//
// 原版怪癖（golden 实证，C++ 侧必须逐条复刻）：
//   ① n==2 分支（0x5A8FDD）：`lea edx,[esi-6]` 算 ebp-7 存 [esp+0x48]，
//     `fild qword` **无符号**读——条目数 <= 7 时 ebp-7 负数 → 无符号巨数 →
//     采样 ftol(rand*巨数*2^-32+7) 恒 ∈ [7,8) > esi=ebp-1 → 拒绝采样死循环
//     （原版死路径，不可采集）。golden 用 ebp=16 规避（采样值域 [7,23) 命中）。
//   ② 0x5A7D00 判定（[0xA8ED2C] 表族）：外/内计数 <= 0 恒返 1（快捷通过）。
//     golden 的表族对象只分配 64B，+0x2E8/+0x2E4 越界写被后续 _bump 清零 →
//     读回 0 → 恒快捷通过。C++ 侧 ProbeFamily 恒 true 对拍。
//   ③ verdict 拒绝路径在当前 golden 灌装下不可达（同上越界清零），
//     故 golden 无 rejection 用例。
//
// 常量（.rdata 取证）：
//   [0x7ED898] = 2^-32 × (1+2^-40)  采样缩放（0x5A8F00 自用，非主流程缩放）
//   [0x7ED838] = 0.7                 n==2 分支阈值
//   [0x7E7FB0] = 7.0                 n==2 特殊采样加数
//   [0x7E48F0] = 1.5                 正态段阈值
//   [0x7E1708] = 2.0 / [0x7E1738] = 0.5
#ifndef RA2_RMG_DEAD_CODE_8F00_H_
#define RA2_RMG_DEAD_CODE_8F00_H_

#include <cstdint>
#include <map>
#include <vector>

#include "map_grid.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 0xABE370 12B 项（golden 配置行 ABE）：count / 子表指针（8B/项）/ 表族 id
// 子表 8B/项：dword0 = 坐标对（x 低 16，y 高 16），dword1 = 域
struct Abe8F00Entry {
  int count = 0;
  int family_id = 0;                                  // [0xABE370+idx*12+8]
  std::vector<std::pair<std::uint32_t, int> > sub;    // (coord_packed, domain)
};

// 双表项（T12 配置行）：表1[idx] = id 数组（P 指针），表2[idx] = 条目数 ebp
struct DualTableRow {
  std::vector<int> ids;    // 每个 = [0xABE370] 项索引
  int ebp = 0;             // 查找循环上界
};

// 这一阶段要问外界的事。0x5A8F00 依赖族（0x5980C0 正态取数 / 0x5A7D00 判定）
// 在 golden 侧前者打桩、后者真身（但 +0x2E8/+0x2E4 越界清零 → 恒返 1），这里
// 按**同一桩语义**抽象：
class DeadCode8F00World {
 public:
  virtual ~DeadCode8F00World() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（采样 + n==2 特殊采样都走它）
  virtual Randomizer& Rng() = 0;

  // —— 0x5980C0 NormalDeviate 桩 ——
  // golden 按 ndseq 序列注入（fyl2x 在 Unicorn 不可信）；C++ 侧同序列。
  // 返回正态分布采样值（double）。
  virtual double NormalDeviate() = 0;

  // —— [0xABE370] 表 ——
  virtual const Abe8F00Entry& AbeEntry(int idx) = 0;

  // —— 0x5A7D00 判定（[0xA8ED2C] 表族） ——
  // 怪癖②：外/内计数<=0 恒返 1（成功）。C++ 侧恒 true 对拍。
  virtual bool ProbeFamily(int family_id) = 0;
};

struct DeadCode8F00Context {
  Randomizer* rng = nullptr;
  DeadCode8F00World* world = nullptr;
  CellCoord base;        // B 指向的基准坐标（bx, by）
  int A = 0;             // ecx = 表族 id
  int mode = 0;          // [esp+4] 栈参1
  // 双表（T12 配置行重建）：下标 = idx（A + 4n），golden 按行直接给
  std::map<int, DualTableRow> table;
};

// 0x5A8F00。零引用死代码，语义按黄金逐条复刻。
// 成功（ret 1）：out2 = 表族 id，out3 = 坐标差 dword，out4 = 命中表项索引。
// 失败（ret 0）：out2 = 0xFFFF，out3 = 0，out4 = -1。
int DeadCode8F00(DeadCode8F00Context& ctx, std::uint32_t* out2,
                 std::uint32_t* out3, std::uint32_t* out4);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_8F00_H_
