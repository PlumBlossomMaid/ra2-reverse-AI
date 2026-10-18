// 死代码复现 —— 0x5A89D0 批量放置主循环（1320B，thiscall 无 this + 1 栈参 ret 4）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A89D0
// 零引用死代码：整个函数从未被任何活代码调用（RMG 流水线 12 阶段不经过它），
// 但内部依赖族（0x5A7D00/0x5A7E90/0x5A7FA0/0x5A6C10/0x5A8F00）共享
// [0xA8ED2C] 表族 —— 本函数是"批量放置调度器"，语义（黄金实证 + 完整汇编
// 逐段确认，batch 928）：
//
//   [esp+4] = 基准坐标 CellStruct*（栈参1，读取 [ebp] 的两个 word 做基准）
//   巨型 0x6058 栈帧（__chkstk 24664 字节），1024 槽 × 0x14B 环形队列在
//   [esp+0x68] 起，timeGetTime 辅助数组在 [esp+0x5068]，count/head/tail 在
//   [esp+0x5C]/[esp+0x60]/[esp+0x64]，placed 计数在 [esp+0x30]。
//
//   ① 1024 槽逐个 call 0x5A99B0 构造：{type=0xFFFF, x=0, 0xFFFF, x=0, -1}
//   ② 主循环 0x5A8A0A：Random()（0xABE890 全局随机器）→ _ftol(v*[0x7EDB28])
//      拒绝采样到 [0,10]（cmp edi,0xA; ja 重抽，11 值类型）→
//      (采样+计数器)%11 → [0xABAD20] 基址 → call 0x5A7D00 探测判定；
//      11 次全失败 → 返回 0（xor al,al）
//   ③ 入队 0x5A8A74：槽 = {type(表项指针), *基准坐标, 0xFFFF, *基准坐标, -1}
//      → rep movsd ×5 拷进 [esp+0x68]+head*0x14 → timeGetTime 存辅助数组 →
//      tail=(tail+1)&0x3FF、count++（count 达 0x400 跳过入队）
//   ④ 处理 0x5A8AE1：count>=0x3FC → 0x5A8E7C 排空分支；否则逐槽：
//      - 槽[0]==0xFFFF → 0x5A8DD6 清理分支（0x5A7E90 + 逐项 0x5A7FA0 +
//        0x5A6C10 + operator delete）
//      - 否则 0x5A7FA0 清理 + 0x5A6C10(ecx=type, edx=&槽[3], 栈参 -1,-1)
//      - LAT 三档映射：[0xABC1D4] 起 0x65 段 → idx；[0xABBEC0] 起 8 段 →
//        idx+0x65；否则 idx=0 → [0xABE370+idx*12] 表项取 count
//      - count<=1 → 0x5A8D06 收尾（不入队）
//      - count>1 → operator new(count×0x14) 临时数组，逐项填
//        {0xFFFF, 0x42D470(...), 0xFFFF, 0x42D470(...), -1}；遍历子表
//        [0xABE370+idx*12+4]（8B/项）：(项&7) 当 0x89F688 八方向表索引
//        合成坐标 → call 0x5A8F00 两次（第一次 mode=1、第二次 mode=4，
//        4 栈参契约）；都成功 → 0x5A8D79 提交（0x5A7FA0×2 + placed++）
//      - 0x5A8D06 收尾：标志 [esp+0x13] 且临时数组非空 → 逐项 rep movsd
//        ×5 入队 + timeGetTime（count 上限 0x400）
//      - 0x5A8E4D 出队：head+1&0x3FF、count--、[esp+0x30]++
//   ⑤ 返回 0x5A8EDF：al = (placed > 1) ? 1 : 0
//
// 被调依赖（全部是同一 [0xA8ED2C] 表族的死代码，golden 侧真身执行）：
//   0x5A7D00 判定 / 0x5A7E90 处理 / 0x5A7FA0 清理 / 0x5A6C10 表派发 /
//   0x5A8F00 取数（本批打桩由用例控制返回值，next 批做真身 C++ 模型）/
//   0x5A99B0 槽构造器 / 0x42D470 双 word 打包（TSV:374 已登记）/ 0x7C8E17
//   operator new / 0x7C8B3D operator delete。
//
// 取证：memory/data/rmg/deadcode_batch_placer_golden.txt（emu_oracle 指令级
// 模拟，12 用例：plain-empty/zero-count/one-count/two-pick0/nine-pick0/
// fail-verdict/ok-verdict/two-blocked/two-levels/two-veto/two-pick1/
// nine-pick1）。常量：0x7EDB28 = 11/(2^32-1)。
#ifndef RA2_RMG_DEAD_CODE_BATCH_PLACER_H_
#define RA2_RMG_DEAD_CODE_BATCH_PLACER_H_

#include <cstdint>
#include <vector>

#include "map_grid.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 放置项槽（0x14 字节 = 5 dword，0x5A99B0 构造器实证）：
//   type（表项指针/0xFFFF 哨兵）、x（基准坐标 dword）、0xFFFF、
//   x（同前）、-1
struct BatchPlacerSlot {
  std::uint32_t type = 0xFFFF;
  std::uint32_t x = 0;
  std::uint32_t sentinel = 0xFFFF;
  std::uint32_t x2 = 0;
  std::uint32_t back = 0xFFFFFFFF;
};

// [0xABE370] 表项（12B）：count + 子表指针 + 扩展字段。
// 子表 8B/项：两个 dword（kx, ky），(ky&7) 做 0x89F688 方向索引。
struct BatchAbeEntry {
  int count = 0;
  std::vector<std::pair<int, int> > sub;   // 8B/项
};

// 这一阶段要问外界的事。被调族（0x5A7D00/0x5A7E90/0x5A7FA0/0x5A6C10/
// 0x5A8F00）在 golden 侧全是真身，这里按**同一桩语义**抽象：
class DeadBatchPlacerWorld {
 public:
  virtual ~DeadBatchPlacerWorld() {}

  // —— 随机 ——
  // 0xABE890 全局 Randomizer（主循环采样 Random()，被调族不直接取数）
  virtual Randomizer& Rng() = 0;

  // —— [0x7E1530] timeGetTime stub ——
  // golden 里覆写成 mov eax,CLOCK_VAL; ret；C++ 侧固定值（辅助数组只写不读）
  virtual std::uint32_t TimeGetTime() = 0;

  // —— [0xABAD20] 类型表探测（0x5A7D00 判定） ——
  // ecx = [0xABAD20]+余数（golden 灌 0 → ecx=余数 0..10）；edx = 基准坐标。
  // 表项对象 {虚表槽 0x9C → 数据指针、+0x2E8 外计数、+0x2E4 内计数}；
  // 计数<=0 → 恒返回 1（成功）。返回 1=探测通过。
  virtual bool ProbeType(int type_index, const CellCoord& base) = 0;

  // —— 0x5A7FA0 清理 / 0x5A6C10 表派发 / 0x5A7E90 处理 ——
  // 真身读 [0xA8ED2C] 表族 + cell/work 状态；C++ 侧同语义回调。
  // 0x5A7E90 仅在 0x5A8DD2 清理分支（0x5A8DDE）被调，处理槽的 cell 标记。
  virtual void CleanType(int type_index, const CellCoord& base) = 0;
  virtual void DispatchType(int type_index, const CellCoord& base) = 0;
  virtual void HandleType(int type_index, const CellCoord& base) = 0;

  // —— [0xABE370] 表 ——
  virtual const BatchAbeEntry& AbeEntry(int idx) = 0;

  // —— 0x5A8F00 取数（本批打桩） ——
  // mode=1/4；返回 1=成功（golden 桩返回按用例 pick_ret 固定）。
  // 成功时 golden 桩写 *栈参3=坐标差 dword、*栈参2=0、*栈参4=3；
  // 失败写 *栈参2=0xFFFF、*栈参3=0、*栈参4=-1。C++ 侧只需要返回值，
  // 输出指针由主循环自己管理（临时数组项）。
  virtual bool PickPlace(int mode, int dir_index, const CellCoord& target) = 0;

  // —— 临时数组内存 ——
  virtual bool AllocTemp() = 0;        // operator new(count×0x14)，0=失败
  virtual void FreeTemp() = 0;         // operator delete
};

struct DeadBatchPlacerContext {
  Randomizer* rng = nullptr;
  DeadBatchPlacerWorld* world = nullptr;
  CellCoord base;                      // [esp+4] 基准坐标（栈参1 指向）
};

// 0x5A89D0。零引用死代码，语义按黄金逐条复刻。
// 返回 al = (placed > 1) ? 1 : 0。
int DeadCodeBatchPlacer(DeadBatchPlacerContext& ctx);

// 环形容器大小（1024 槽 × 0x14B）
constexpr int kBatchPlacerRingSize = 1024;

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_BATCH_PLACER_H_