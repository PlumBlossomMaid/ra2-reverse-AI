// 死代码复现 —— 0x5A3A60 全图扫描 + Y*side+X 线性索引（125B，零取数点）
// （batch 1231）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A3A60（125B）
// 调用约定：cdecl 无参 ret 0（无 ecx/this 使用——纯工具函数）。
// 零引用死代码（TSV:1007 已 semantics 登记，batch 319；2026-10-21 复刻）。
//
// 语义（完整汇编逐段核对 + golden 12 行实证，deadcode_5a3a60_golden.txt）：
//   CellIterator_Reset(0x578350, ecx=0x87F7E8)
//   loop（0x5A3A78 起）：
//     cell = CellIterator_Next(0x578290, ecx=0x87F7E8)
//     if (cell == NULL) break（0x5A3A7D test eax,eax / je 0x5A3AD9）
//     ecx = [cell+0x24]（坐标打包 dword：低16=X、高16=Y）
//     [esp+4] = ecx
//     Y = movsx word [esp+6]（高16 符号扩展）
//     idx = Y * [0x89C2DC] + X（imul 边长 + movsx cx 低16 符号扩展）
//     p = [0xABED10] + idx*0x50（lea eax,[eax+eax*4]; shl eax,4 = idx*0x50）
//     p+0x20 = [0x7ED7F8]；p+0x24 = [0x7ED7FC]
//     p+0x28 = [0x7ED7F0]；p+0x2C = [0x7ED7F4]
//   ★零取数点：全程不调 Randomizer（golden next 字段实证：同种子
//     所有用例 next 相同 = 调用前后 RNG 状态不变）
//
// 原版怪癖（golden 实证，C++ 侧逐条复刻）：
//   ① 零取数点（不消耗 RNG——同 0x5A6BA0）；
//   ② CellIterator_Reset/Next 双轮遍历（0x578350/0x578290——活代码
//      batch 006 家族，本批打桩：指针表顺序取 cell，NULL 终止）；
//   ③ idx = Y*side + X 线性索引（imul 边长——RMG 工作数组寻址约定）；
//   ④ 坐标解包：[cell+0x24] 低16=X 高16=Y，均 movsx 符号扩展；
//   ⑤ 每格写 p+0x20..0x2C 四个 dword（.rdata 常量 [0x7ED7F0..FC]，
//      顺序照抄：+0x20=f8、+0x24=fc、+0x28=f0、+0x2C=f4）。
#ifndef RA2_RMG_DEAD_CODE_5A3A60_H_
#define RA2_RMG_DEAD_CODE_5A3A60_H_

#include <cstddef>
#include <cstdint>

namespace ra2 {
namespace rmg {

// 这一阶段要问外界的事（CellIterator 打桩——按坐标顺序返回下一格）：
class DeadCode5A3A60World {
 public:
  virtual ~DeadCode5A3A60World() {}

  // —— CellIterator_Next(0x578290) 打桩 ——
  // 原版：返回下一格 cell 指针（NULL 终止）；C++ 侧 NextCell(x,y)
  // 返回「还有下一格」布尔并写出坐标。golden 指针表顺序取 cell。
  virtual bool NextCell(int& x, int& y) = 0;
};

struct DeadCode5A3A60Context {
  DeadCode5A3A60World* world = nullptr;
  std::int32_t side = 0;   // [0x89C2DC] MapGen 边长（idx = Y*side+X）
  // .rdata 四常量 [0x7ED7F0/+4/+8/+C]（写 p+0x28/+0x2C/+0x20/+0x24）
  std::uint32_t c_f0 = 0;  // [0x7ED7F0] → p+0x28
  std::uint32_t c_f4 = 0;  // [0x7ED7F4] → p+0x2C
  std::uint32_t c_f8 = 0;  // [0x7ED7F8] → p+0x20
  std::uint32_t c_fc = 0;  // [0x7ED7FC] → p+0x24
  // [0xABED10] work 数组：side² 项 × 0x50B；C++ 侧用 4 字段建模
  // 写入段 p+0x20..0x2C。
  struct WorkSlot {
    std::uint32_t f20 = 0;
    std::uint32_t f24 = 0;
    std::uint32_t f28 = 0;
    std::uint32_t f2C = 0;
  };
  WorkSlot* work = nullptr;  // side² 项（索引 idx = Y*side+X）
  std::size_t work_size = 0;
};

// 0x5A3A60。零引用死代码，语义按黄金逐条复刻。零取数点（不调 RNG）。
// 全图扫描：每格（经 CellIterator 打桩）写 work[Y*side+X] 的
// +0x20..0x2C 四个 dword。
void DeadCode5A3A60(const DeadCode5A3A60Context& ctx);

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_5A3A60_H_