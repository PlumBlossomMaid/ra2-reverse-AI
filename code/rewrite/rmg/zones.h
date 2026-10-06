// 移动区子系统：地面类别 → 连通区洪泛 → 每种移动区的「区类型」表
//
// 阶段 6 的区域选择全靠它：`PlaceStartPoints` 开场调 `0x56C510` 重建整套区，
// 再用「最大区在移动区 5 下的区类型」把地图切成「能放出生点的地方」。
//
// 取证（gamemd.exe）：
//   MapClass::Update_Pathfinding_1   0x56C510   （重建：洪泛 + 桶 + CSR + 区类型表）
//   scanline flood                   0x56CB90
//   MapClass::GetMovementZoneType    0x56D230
//   MapClass::CellToIndex            0x56D3F0   （y*side+x，负数夹 0、超界夹末尾）
//   CellClass::RecalcLandType        0x483C80
//   MapClass::IsWithinUsableArea     0x578460
//   TileTypeClass -> GroundType      0x544BE0，表 TerrainToGround 0x8288E4
//   MoveTable                        0x82A594（13 行 × 8 列 dword，行距 0x20）
//   区表分配与初值                    0x567110-0x567182（side² 项，land=7、level=0）
//   跨区连接桶                        0x565859-0x5658DD（256 个，每个 0x18 字节，
//                                     GrowthStep 被改写成 0x14）
// 详见 docs/random-map/movement-zones.md。
#ifndef RA2_RMG_ZONES_H_
#define RA2_RMG_ZONES_H_

#include <cstdint>
#include <vector>

namespace ra2 {
namespace rmg {

// ── 地面类别（`CellClass +0x4C`），0..7 ────────────────────────────────────
// 随机地图跑到阶段 6 时只可能出现 0 / 3 / 4 / 6 / 7 五个值，
// 1（Road/桥）、2（Tunnel）、5（矿）都需要 overlay 才可能出现。
enum LandCategory : std::uint8_t {
  kLandClear = 0,
  kLandRoad = 1,
  kLandTunnel = 2,
  kLandBeach = 3,
  kLandWater = 4,
  kLandTiberium = 5,
  kLandImpassable = 6,   // Rock / Wall，即 Ground[].Foot <= 0.01
  kLandOffMap = 7,
};

// ── GroundType（`CellClass +0xEC`），12 项，名字就是 rules.ini 的小节名 ────
enum GroundType : int {
  kGroundClear = 0, kGroundRoad = 1, kGroundWater = 2, kGroundRock = 3,
  kGroundWall = 4, kGroundTiberium = 5, kGroundBeach = 6, kGroundRough = 7,
  kGroundIce = 8, kGroundRailroad = 9, kGroundTunnel = 10, kGroundWeeds = 11,
};
inline constexpr int kGroundTypeCount = 12;

// `TerrainToGround`（0x8288E4，16 个 dword）：`.tmp` 子块头 +0x29 → GroundType。
// ⚠️ 原版取的是**有符号**字节（0x544C05 `movsx`）后直接当下标，
//    真实数据里只出现 9/10/13/14/15，不会越界。
extern const int kTerrainToGround[16];

// `MoveTable`（0x82A594）：行 = MovementZone（13 种），列 = 地面类别（8 种）。
// 值 1 = 走得通；2 / 3 = 走不通。随机地图只用得到第 0 行与第 5 行。
inline constexpr int kMovementZoneCount = 13;
extern const int kMoveTable[kMovementZoneCount][8];

// ── 地面类别的计算（`CellClass::RecalcLandType` 0x483C80 的随机地图子集）──
//
// 阶段 6 之前格子上既没有 overlay（`+0x44` 还是 -1）也没有 object（`+0xE4` 为 0），
// 所以原版那五条 overlay 分支与那条矿物遍历都走不到，只剩这四条。
// `ground_foot` 是 rules.ini 里 12 个地面小节的 `Foot=`（0..1），由调用方注入。
std::uint8_t RecalcLandType(bool within_usable_area, int ground_type,
                            const float* ground_foot);

// ── 矩形与可用区判定 ──────────────────────────────────────────────────────
struct MapRect {
  int x = 0, y = 0, w = 0, h = 0;
};

// `MapClass::Set_LocalSize`（0x654490 → 0x567230）：与整图矩形求交后逐字段夹取。
// 整图矩形恒为 `{0, 0, W, H}`（`0x5662D8/0x5662E1` 强行清零 X/Y）。
MapRect ClampLocalRect(const MapRect& want, int map_w, int map_h);

// `MapClass::IsWithinUsableArea`（0x578460）。
// `use_height` 为真时先取格子的层高/坡道再判界——`RecalcLandType` 传的就是真。
// 出图格子在原版里会退到共享哑元 `0xABDC50`，其 `+0x11B/+0x11C` 是上一次的残留；
// 调用方给出图格子传 `level = 0, ramp = 0` 即可（阶段 1 已把全图清成 4/0，
// 而哑元只在这里被读，随后立刻被覆盖，见文档）。
bool IsWithinUsableArea(int map_w, const MapRect& local, int x, int y,
                        int level, int ramp, bool use_height);

// ── 每格 4 字节的地形表（`MapClass+0x68`，`side²` 项）────────────────────
#pragma pack(push, 1)
struct ZoneCell {
  std::uint8_t land = kLandOffMap;   // byte[0]，0x567172 的初值就是 7
  std::uint8_t level = 0;            // byte[1]
  std::uint16_t zone = 0;            // word[2]，洪泛填
};
#pragma pack(pop)
static_assert(sizeof(ZoneCell) == 4, "ZoneCell 必须正好 4 字节");

// 洪泛的产物。
struct ZoneResult {
  int zone_count = 0;                 // `MapClass+0x4C`，= 区个数 + 1（含 0 号占位）
  std::vector<std::uint8_t> land_of;  // 每个区的地面类别；`land_of[0] = 7`
  // 13 × zone_count 的「区编号 → 区类型」表（`MapClass+0x18..+0x48`）。
  // `[mz][0] = 0xFFFF`；走不通的区恒为 1；走得通的从 2 起编号。
  std::vector<std::vector<std::uint16_t>> zone_type;
  int largest_zone = -1;              // `0x56C510` 的返回值：格数最多的那个区的编号
};

class ZoneTable {
 public:
  // `map_w` / `map_h` 是**整图矩形**的宽高（`MapClass+0xF4/+0xF8`，
  // 随机地图里是 `W+4` / `H+12`）。
  ZoneTable(int map_w, int map_h);

  int map_w() const { return map_w_; }
  int map_h() const { return map_h_; }
  int side() const { return map_w_ + map_h_ + 1; }   // 0x56714A
  int count() const { return side() * side(); }      // 0x56714E imul

  // `MapClass::CellToIndex`（0x56D3F0）：⚠️ 负数夹到 **0**，不是夹到末尾。
  int CellToIndex(int x, int y) const;

  ZoneCell& at(int idx) { return cells_[kPad + idx]; }
  const ZoneCell& at(int idx) const { return cells_[kPad + idx]; }
  ZoneCell& at(int x, int y) { return at(CellToIndex(x, y)); }
  const ZoneCell& at(int x, int y) const { return at(CellToIndex(x, y)); }

  // 整表迭代（不含 padding 哨兵），供导出/对拍用。
  std::vector<ZoneCell>::iterator begin() { return cells_.begin() + kPad; }
  std::vector<ZoneCell>::iterator end() { return cells_.begin() + kPad + count(); }
  std::vector<ZoneCell>::const_iterator begin() const {
    return cells_.begin() + kPad;
  }
  std::vector<ZoneCell>::const_iterator end() const {
    return cells_.begin() + kPad + count();
  }

  // `MapClass::Update_Pathfinding_1`（0x56C510）。
  ZoneResult Rebuild();

  // `MapClass::GetMovementZoneType`（0x56D230，`follow_bridge = false` 的短路径）
  std::uint16_t GetMovementZoneType(const ZoneResult& r, int x, int y,
                                    int movement_zone) const;

 private:
  // 洪泛会读到本段左右各外扩一格、以及上下两整行——全都靠「出图格子 land == 7」
  // 停住。这里在表两头各留一圈 `land == 7` 的哨兵，纯粹是为了让越界读在 C++ 里
  // 也是良定义的；原版是裸指针算术，行为等价。
  int kPad = 0;
  int map_w_ = 0, map_h_ = 0;
  std::vector<ZoneCell> cells_;

  // 256 个跨区连接桶，桶号 `((a & 0xF) << 4) | (b & 0xF)`，桶内线性去重。
  std::vector<std::vector<std::uint32_t>> buckets_;
  std::uint32_t last_neighbour_ = 0;   // `[0xABDE8C]`

  int Flood(int idx, std::uint16_t zone, int* run_len);
  void RegisterLink(std::uint32_t neighbour, std::uint16_t zone);
};

}  // namespace rmg
}  // namespace ra2

#endif  // RA2_RMG_ZONES_H_
