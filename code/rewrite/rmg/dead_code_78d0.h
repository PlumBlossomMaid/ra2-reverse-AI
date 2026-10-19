// 死代码复现 —— 0x5A78D0 随机抽样 + 小块批量放置（batch 931）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A78D0（614B）
// 调用约定：thiscall 无 this + 1 栈参（宿主对象指针 edi = [esp+0xA8]）ret 4。
// 零引用死代码（TSV:1022 已 semantics 登记，batch 322；2026-10-15 补取证
// 修正为「随机抽样 + 小块批量放置」）。
//
// 语义链（完整汇编逐段核对 + golden 5 用例实证，deadcode_78d0_golden.txt）：
//   · 宿主对象 edi：+4 = 模板项数组（4B/项 dword，X=低16 有符号、Y=高16
//     有符号）、+0x10 = 循环上限 limit。
//   · 外层循环双计数（0x5A7985-0x5A7B26）：
//     - [esp+0x10] 成功放置轮数，上限 ebp = limit/50（0x51EB851F magic
//       配 sar edx,4——标准 /100 是 sar 5，这里是原版怪癖 /50，golden
//       all-fail limit=100 → ebp=2 实锤）
//     - [esp+0x30] 总轮数，上限 esi = ebp*10（0x5A798B lea ebp*5 / shl 1）
//     - 两计数任一达上限退出
//   · 每轮（0x5A79B8-0x5A79FE）：拒绝采样 [0, limit-1]（fild qword 无符号
//     读 Random + fmul [0x7ED898]=2^-32 + ja 无符号重抽）→ [edi+4][采样]
//     → 拆 X/Y 模板坐标。
//   · 模板循环（0x5A7A36-0x5A7A67）：edi=0..4 最多 5 项，栈上模板数组
//     [esp+0x60] 起 0x10 间隔（项 = {a=[ptr-4], b=[ptr]}，a 序列
//     {6,4,4,4,2}、b 序列 {4,6,4,2,4}——★push edi 前 [esp+0x58]=6 物理
//     落在 push 后 [esp+0x5C]（项 0 的 a），golden ZN d2 首值 6 实锤）
//     → 组装 {X, Y, a, b} 调 0x5A7B40
//     （菱形校验模板第三实例——golden 打桩，azn_seq 序列）→ 成功 bl=1
//     提前退出。
//   · bl=0 全失败 → 只 [esp+0x30]++ 跳过本轮（golden all-fail 用例：
//     5 个 0 耗尽后第 6 次起返 1 → 第 2 轮成功放置）。
//   · 成功（0x5A7A71-0x5A7B08）：0..5 拒绝采样（fmul [0x7EDA40]=
//     6/2^32×(1+ε)，ja 重抽）→ 宿主 = [0xAA10A4] + 采样 + 8 →
//     双重循环（外层 Y 上限 = tmpl_b>>1 有符号 sar、内层 X 上限 =
//     tmpl_a>>1）：坐标 = {X+2*esi, Y+2*edi} → 0x5A6C10（golden 打桩，
//     记录 ecx=宿主、edx=&坐标结构、栈参 -1/-1）。
//   · 返回 EAX 无意义（EAX 残渣），C++ 返回 0 对拍。
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 外层双计数上限（limit/100 成功轮 + limit/100*10 总轮）——all-fail
//     用例 5 个 azn=0 耗尽后返 1 触发第 2 轮放置，rng=5 实锤两轮。
//   ② 模板数组 a/b 序列 {4,4,4,4,2}/{0,6,4,2,4} 是栈上常数，嵌入
//     0x5A78D0 开段（0x5A78E3-0x5A7A20 逐 dword mov）。
//   ③ 宿主基址 [0xAA10A4] 在 .data 未初始化区（golden 按用例灌值）。
//   ④ 0x5A7B40/0x5A6C10 golden 打桩：前者裸 ret（无栈参清理）、后者
//     ret 8（两栈参）——打桩 ret 数必须与实际一致（0x5A7B40 用 4 会错位
//     UC_ERR_READ_UNMAPPED，已实证修复）。
//
// 常量（.rdata 取证）：[0x7ED898] = 2^-32×(1+2^-40)（拒绝采样缩放）、
//   [0x7EDA40] = 6/2^32×(1+2^-37+2^-41)（0..5 采样比例）、
//   [0xAA10A4] = 宿主基址全局（未初始化）。
#ifndef RA2_RMG_DEAD_CODE_78D0_H_
#define RA2_RMG_DEAD_CODE_78D0_H_

#include <cstdint>
#include <vector>

#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（被调族按 golden 桩语义抽象）：
class DeadCode78D0World {
 public:
  virtual ~DeadCode78D0World() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（拒绝采样）
  virtual Randomizer& Rng() = 0;

  // —— 0x5A7B40 菱形校验（模板第三实例，打桩） ——
  // 参数 = 模板坐标 {tx, ty} + 模板项 {a, b}。golden 按 azn_seq 序列返回
  // （耗尽返 1）；C++ 侧 ProbeArea 实现同一序列语义。
  virtual bool ProbeArea(int tx, int ty, int tmpl_a, int tmpl_b) = 0;

  // —— 0x5A6C10 表查询放置（打桩） ——
  // ecx = 宿主（[0xAA10A4]+采样+8）、坐标 {x, y}、栈参 -1/-1。
  // golden 记录调用序列，返回 0 固定；C++ 侧 PlaceType 记录同序列。
  virtual void PlaceType(int host, int x, int y) = 0;
};

struct DeadCode78D0Context {
  Randomizer* rng = nullptr;
  DeadCode78D0World* world = nullptr;
  std::uint32_t host_limit = 0;        // [edi+0x10] 循环上限
  std::vector<std::uint32_t> host_arr; // [edi+4] 模板项数组
  std::uint32_t ab10a4 = 0;            // [0xAA10A4] 宿主基址
};

// 0x5A78D0。零引用死代码，语义按黄金逐条复刻。返回 0（EAX 无意义）。
int DeadCode78D0(const DeadCode78D0Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_78D0_H_