// 死代码复现 —— 0x5A82E0 随机布点放置器（1081B，thiscall + 1 栈参 ret 4）
//
// 原版地址（gamemd.exe，ImageBase 0x400000）：0x5A82E0
// 零引用死代码：0x45E7B0 名字表查不到时 `je 0x5A86E1` 跳过 attempt++，
// 外层死循环——正是它从未被启用的根因（原版怪癖，照抄不修）。
//
// 语义（黄金实证 + 完整汇编逐段确认，batch 927）：
//   [esp+4]=坐标 CellStruct*（thiscall，ecx=this，this+0x38 参与判定）
//   ① 选数量 n：拒绝采样 Random()*5/(2^32-1)+8.0，>12 重摇 → n∈[0,12]
//   ② 基准地形 = GetCellAt(坐标) 的 cell+0x11B（movsx 有符号字节）
//   ③ 阈值：this+0x38==0 → lo=15；否则 lo=0；hi=35
//   ④ 36 项表 [0x82B1B0..0x82B240)（ABAN01..ABAN15+CA0001..CA0021 类型名）
//      经容器（0x5AD6F0 构造 + 虚表槽 8 = SetCapacity 0x49EA70 扩容）
//      逐项收集——cdecl 2 栈参 ret 8 容器，10 个一批扩容
//   ⑤ owner = FindIndexOfName("Neutral" 0x82BA08) → FindByCountryIndex
//      （0x5117D0 → 0x502D30）
//   ⑥ 外层循环（attempt < 2n 且 placed < n）：
//      - idx = 拒绝采样 Random()*(hi-lo+1)*kInvUintMax+lo，>hi（无符号 ja）重摇
//      - 候选 = 容器[idx] → 0x45E7B0 全局名字表（[0xA83C6C] 每项 +0x24
//        内联字符串，strcmpi）→ 类型索引 or -1（-1 死循环怪癖）
//      - operator new(0x720) → BuildingClass(0x43B740, type, owner)
//      - 内层 tries<60：r1/r2 各拒绝采样 [0,14]（*15/(2^32-1) → >14 重摇）
//        x' = 坐标.x + r2-7；y' = 坐标.y + r1-7（int16）
//        类型虚表 +0x90(1) 占地表 → 逐格 (0x7FFF,0x7FFF) 收尾：
//          合成格 = 占地偏移 + (x',y')（int16 回绕），GetCellAt → 校验链：
//          +0xE4 无占用 && IsPlain(0x486380) && 地形==基准 && +0xEC!=3
//          && IsWithinUsableArea(cell,1)（0x578540）
//        全过 → 像素 (x'<<8)+0x80,(y'<<8)+0x80 → 虚表 +0xD8(&coord,0) 落地
//        成功 → placed++、容器前移压缩（删 idx）、dec hi（[esp+0x18]--）
//      60 次失败 → 虚表 +0x20(1) 销毁
//  ⑦ 收尾：容器缓冲 delete
//
// 取证：memory/data/rmg/deadcode_placer_golden.txt（emu_oracle 指令级模拟，
// 8 用例：simple/all38/blocked/levels/veto/unplace/halfname/no40；
// name1 因触发原版死循环怪癖移除登记）。常量：0x7EDAB0=5/(2^32-1)、
// 0x7E48E0=8.0、0x7EDB18=15/(2^32-1)、0x7ED898=kInvUintMax。
#ifndef RA2_RMG_DEAD_CODE_PLACER_H_
#define RA2_RMG_DEAD_CODE_PLACER_H_

#include <cstdint>
#include <vector>

#include "blob_paint.h"   // PaintWorkCell：+0x45
#include "map_grid.h"
#include "randomizer.h"

namespace ra2 {
namespace rmg {

// 占地表的一项（BuildingTypeClass 虚函数 +0x90(1) 返回的 short 对，
// 以 (0x7FFF, 0x7FFF) 收尾）。分量 int16，与锚点相加按 int16。
struct PlacerFoundationOffset {
  std::int16_t dx = 0;
  std::int16_t dy = 0;
};

// 这一阶段要问外界的事。建筑对象是引擎的东西（BuildingClass 0x720 字节、
// 三个虚函数），复刻侧只用一个不透明句柄表示：< 0 = operator new 失败。
class DeadPlacerWorld {
 public:
  virtual ~DeadPlacerWorld() {}

  // —— 格子（0x5A82E0 内层校验链） ——
  virtual int Level(const CellCoord& c) = 0;                // +0x11B，有符号字节
  virtual bool IsPlain(const CellCoord& c) = 0;             // 0x486380
  virtual bool HasOccupier(const CellCoord& c) = 0;         // +0xE4 != 0
  virtual int Field0xEC(const CellCoord& c) = 0;            // +0xEC，==3 否决
  virtual bool WithinUsableArea(const CellCoord& c) = 0;    // 0x578540(cell,1)

  // —— 名字表 ——
  // 0x45E7B0：全局名字表（[0xA83C6C] 每项 +0x24 内联名，strcmpi 比较）
  // 名字命中返回类型索引，未命中返回 -1（⚠️ -1 在原版里触发外层死循环）
  virtual int LookupTypeIndex(const std::string& name) = 0;

  // —— 建筑 ——
  // operator new(0x720)。返回 0 表示分配失败（对象为 NULL，后续直接炸——
  // 原版 0x5A849E je 0x5A84C7 后内层 `mov ecx,[ebp+0x520]` 解引用空指针；
  // 死代码从未触发，C++ 侧同样不防御）。
  virtual bool AllocBuildingMemory() = 0;
  // BuildingClass::BuildingClass(type_obj, owner)（0x43B740）
  // type_obj = [0xA83C6C][LookupTypeIndex 返回值]
  virtual int CreateBuilding(int type_index, int owner) = 0;
  // 虚函数 +0x90(1)：占地表
  virtual const std::vector<PlacerFoundationOffset>& Foundation(int handle) = 0;
  // 虚函数 +0xD8(&coord, 0)：落地。coord = (x*256+128, y*256+128, 0)
  virtual bool Unlimbo(int handle, const CellCoord& at) = 0;
  // 虚函数 +0x20(1)：没放下去就销毁
  virtual void Destroy(int handle) = 0;

  // 0x5117D0 FindIndexOfName("Neutral") → 0x502D30 FindByCountryIndex
  virtual int FindNeutralOwner() = 0;
};

struct DeadPlacerContext {
  Randomizer* rng = nullptr;
  DeadPlacerWorld* world = nullptr;
  CellCoord anchor;         // [esp+4] 坐标（thiscall 栈参）
  int this38 = 0;           // this+0x38：==0 → lo=15（只用 CA0001..CA0021）
  std::vector<std::string>* type_names = nullptr;   // 36 项表（ABAN01..CA0021）
};

// 0x5A82E0。零引用死代码，语义按黄金逐条复刻。
void DeadCodeRandomPlacer(DeadPlacerContext& ctx);

// 36 项表（原版 [0x82B1B0..0x82B240) 指向的字符串，golden 实证）
inline const char* const kPlacerTypeNames[36] = {
    "ABAN01", "ABAN02", "ABAN03", "ABAN04", "ABAN05", "ABAN06",
    "ABAN07", "ABAN08", "ABAN09", "ABAN10", "ABAN11", "ABAN12",
    "ABAN13", "ABAN14", "ABAN15", "CA0001", "CA0002", "CA0003",
    "CA0004", "CA0005", "CA0006", "CA0007", "CA0008", "CA0009",
    "CA0010", "CA0011", "CA0012", "CA0013", "CA0014", "CA0015",
    "CA0016", "CA0017", "CA0018", "CA0019", "CA0020", "CA0021",
};

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_DEAD_CODE_PLACER_H_