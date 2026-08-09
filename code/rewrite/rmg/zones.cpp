#include "zones.h"

#include <cstdlib>

namespace ra2 {
namespace rmg {

// 0x8288E4，16 个 dword
const int kTerrainToGround[16] = {0, 8, 8, 8, 8, 10, 9, 3,
                                  3, 2, 6, 1, 1, 0, 7, 3};

// 0x82A594，13 行 x 8 列 dword，行距 0x20，末尾 0x82A734
const int kMoveTable[kMovementZoneCount][8] = {
    {1, 2, 2, 2, 2, 2, 2, 3},   //  0
    {1, 1, 2, 2, 2, 2, 2, 3},   //  1
    {1, 1, 1, 2, 2, 2, 2, 3},   //  2
    {1, 1, 1, 1, 1, 1, 2, 3},   //  3
    {1, 1, 2, 1, 1, 2, 2, 3},   //  4
    {1, 2, 2, 1, 1, 2, 2, 3},   //  5
    {1, 1, 1, 2, 2, 2, 1, 3},   //  6
    {1, 2, 2, 2, 2, 1, 2, 3},   //  7
    {1, 1, 1, 2, 2, 1, 2, 3},   //  8
    {1, 1, 1, 1, 1, 1, 1, 3},   //  9
    {2, 2, 2, 2, 1, 2, 2, 3},   // 10
    {2, 2, 2, 1, 1, 2, 2, 3},   // 11
    {1, 1, 1, 2, 2, 2, 2, 3},   // 12
};

// -- 0x483C80 在随机地图下的子集 --------------------------------------------
std::uint8_t RecalcLandType(bool within_usable_area, int ground_type,
                            const float* ground_foot) {
  if (!within_usable_area) return kLandOffMap;            // 0x483C99
  // 阶段 6 之前格子上没 overlay（+0x44 == -1），五条 overlay 分支整段跳过
  if (ground_type == kGroundWater) return kLandWater;     // 0x483D30
  if (ground_type == kGroundBeach) return kLandBeach;     // 0x483D45
  // 0x483D57 `fld dword Ground[gt].Foot`（步长 36）后与 **double** 0.01
  // （0x7E3808 = 7b14ae47e17a843f）比，`test ah,0x41 / je` 表示 C3|C0 置位
  // （小于或等于，或无序）才落 6。
  if (static_cast<double>(ground_foot[ground_type]) <= 0.01) {
    return kLandImpassable;                               // 0x483D6B
  }
  // 阶段 6 之前 +0xE4 == 0（格子上没 object），那条找矿的遍历也整段跳过
  return kLandClear;                                      // 0x483DD4
}

// -- 0x421B60 求交 + 0x567230 逐字段夹取 ------------------------------------
MapRect ClampLocalRect(const MapRect& want, int map_w, int map_h) {
  // 0x421B60：与整图矩形 {0, 0, map_w, map_h} 求交。四个宽高任一 <= 0 就整体
  // 失败，失败时输出全 0（0x421C68 之后那段），随后照样进夹取。
  MapRect r;
  if (want.w > 0 && want.h > 0 && map_w > 0 && map_h > 0) {
    int x = 0, y = 0, w = map_w, h = map_h;
    if (x < want.x) { w += x - want.x; x = want.x; }        // 0x421BB6
    if (w >= 1) {
      if (y < want.y) { h += y - want.y; y = want.y; }      // 0x421BD6
      if (h >= 1) {
        if (x + w > want.x + want.w) w = want.x + want.w - x;   // 0x421BFA
        if (w >= 1) {
          if (y + h > want.y + want.h) h = want.y + want.h - y; // 0x421C1E
          if (h >= 1) { r.x = x; r.y = y; r.w = w; r.h = h; }
        }
      }
    }
  }
  // 0x56726E 起：x/y 下夹 2，w/h 上夹（整图边长 - 起点 - 2 / - 6）
  int x = r.x > 2 ? r.x : 2;                                // 0x567270 jg
  int y = r.y > 2 ? r.y : 2;                                // 0x567282 jg
  int wmax = (map_w - x) - 2;                               // 0x56729A lea -2
  int hmax = (map_h - y) - 6;                               // 0x5672BD add -6
  int w = r.w >= wmax ? wmax : r.w;                         // 0x5672A3 jge
  int h = r.h >= hmax ? hmax : r.h;                         // 0x5672C0 jge
  MapRect out;
  out.x = x; out.y = y; out.w = w; out.h = h;
  return out;
}

// -- 0x578460 ---------------------------------------------------------------
bool IsWithinUsableArea(int map_w, const MapRect& local, int x, int y,
                        int level, int ramp, bool use_height) {
  int adj = 0;
  if (use_height) {
    adj = static_cast<int>(static_cast<signed char>(level));   // 0x5784A8 movsx
    // 0x5784B4：`+0x11C != 0`（这一格是坡）时，落在下边界附近的再抬一格
    if (ramp != 0 && x + y < map_w + local.y * 2 + 4 + adj) ++adj;
  }
  if (x + y <= map_w + local.y * 2 + adj) return false;                  // 0x5784E1
  if (x + y > map_w + (local.y + local.h) * 2 + 2 + adj) return false;   // 0x5784FE
  if (x - y >= (local.x + local.w) * 2 - map_w) return false;            // 0x578518
  if (y - x >= map_w - local.x * 2) return false;                        // 0x578523
  return true;
}

// -- ZoneTable --------------------------------------------------------------
ZoneTable::ZoneTable(int map_w, int map_h) : map_w_(map_w), map_h_(map_h) {
  // 洪泛的游标会越过表两端（上一行 / 下一行、以及左右各多探一格），原版就是
  // 拿裸指针在堆上乱走。这里前后各留一圈 padding，让越界读在 C++ 里也有定义；
  // padding 的 land 恒为 7（出图），洪泛碰到它必然停，行为与原版一致
  // ——除非原版真的读到了别的堆数据，那属于真·未定义，谁也复刻不了。
  kPad = side() + 4;
  cells_.assign(static_cast<std::size_t>(count()) + 2 * kPad, ZoneCell());
  buckets_.assign(256, std::vector<std::uint32_t>());
}

int ZoneTable::CellToIndex(int x, int y) const {
  const int idx = y * side() + x;        // 0x56D2F1 / 0x56C6E6
  if (idx < 0) return 0;                 // 负下标夹到 0（不是夹到末尾）
  if (idx >= count()) return count() - 1;
  return idx;
}

void ZoneTable::RegisterLink(std::uint32_t neighbour, std::uint16_t zone) {
  const std::uint32_t pair =
      (neighbour << 16) | static_cast<std::uint32_t>(zone);
  const std::uint32_t bi = ((neighbour & 0xF) << 4) | (zone & 0xF);
  // 去重循环写成 `while (i = i - 1, uVar14 = uVar11, -1 < i)`，那个逗号赋值
  // 连桶为空时也执行一次；所以只要谓词过了，`[0xABDE8C]` 就一定被改写。
  last_neighbour_ = neighbour;
  std::vector<std::uint32_t>& b = buckets_[bi];
  for (std::size_t i = 0; i < b.size(); ++i) {
    if (b[i] == pair) return;
  }
  b.push_back(pair);                     // GrowthStep 20（0x5658CC），Add 不会失败
}

namespace {
inline int AbsDiff(int a, int b) { return a > b ? a - b : b - a; }
}  // namespace

int ZoneTable::Flood(int p, std::uint16_t zone, int* run_len) {
  ZoneCell* T = cells_.data() + kPad;
  const int st = side();
  const std::uint8_t land = T[p].land;
  const bool is_rock = (land == kLandImpassable);   // bVar19
  int h = T[p].level;

  // 向左扩：层高差 <= 1（0x56CBCA `cmp eax,2 / jge`）
  int L = p;
  do {
    if (AbsDiff(T[L].level, h) > 1) break;
    T[L].zone = zone;
    h = T[L].level;
    --L;
  } while (T[L].land == land);

  {  // 左端点的跨区登记
    const std::uint32_t nz = T[L].zone;
    if (nz != 0 && (AbsDiff(T[L].level, h) < 2 || is_rock) &&
        nz != last_neighbour_ && nz != zone) {
      RegisterLink(nz, zone);
    }
  }

  // 向右扩：门限**变成 4**（0x56CCE2 `cmp eax,4 / jge`），且起手的 `h` 是左扫
  // 留下的「最左那格」的层高，不是 p 的。两处都照抄，不要"修正"。
  int R = p;
  while (T[R].land == land && AbsDiff(T[R].level, h) < 4) {
    T[R].zone = zone;
    h = T[R].level;
    ++R;
  }

  {  // 右端点的跨区登记
    const std::uint32_t nz = T[R].zone;
    if (nz != 0 && (AbsDiff(T[R].level, h) < 2 || is_rock) &&
        nz != last_neighbour_ && nz != zone) {
      RegisterLink(nz, zone);
    }
  }

  int painted = (R - L) - 1;
  *run_len = (R - p) - 1;      // 右扫一格都没走成时是 -1，外层会往回退一格

  // 上下两行各扫 [L, R]（比涂到的区间左右各多一格）
  int upper = L - st;
  const int upper_end = R - st - 1;
  int lower = L + st;
  const int lower_end = R + st;
  int sub = 0;

  for (;;) {
    if (upper > upper_end + 1) {          // 0x56CE4D `pcVar13 + 4 < pcVar18`
      while (lower <= lower_end) {
        int ref;
        if (lower < lower_end - 1)       ref = lower - st + 1;
        else if (lower == lower_end - 1) ref = lower - st;
        else                             ref = lower - st - 1;
        const std::uint32_t nz = T[lower].zone;
        if (nz == 0) {
          if (T[lower].land == land &&
              AbsDiff(T[lower].level, T[ref].level) < 2) {
            painted += Flood(lower, zone, &sub);
            lower += sub;
          } else {
            ++lower;
          }
        } else {
          if (nz != zone && nz != last_neighbour_ &&
              (AbsDiff(T[lower].level, T[ref].level) < 2 || is_rock)) {
            RegisterLink(nz, zone);
          }
          ++lower;
        }
      }
      return painted;
    }

    int ref;
    if (upper < upper_end)       ref = upper + st + 1;
    else if (upper == upper_end) ref = upper + st;
    else                         ref = upper + st - 1;
    const std::uint32_t nz = T[upper].zone;
    if (nz != 0) {
      if (nz != zone && nz != last_neighbour_ &&
          (AbsDiff(T[upper].level, T[ref].level) < 2 || is_rock)) {
        RegisterLink(nz, zone);
      }
      ++upper;
    } else if (T[upper].land != land ||
               AbsDiff(T[upper].level, T[ref].level) > 1) {
      ++upper;
    } else {
      painted += Flood(upper, zone, &sub);
      upper += sub;
    }
  }
}

ZoneResult ZoneTable::Rebuild() {
  // (1) 清空 256 个跨区连接桶（0x56C55D 逐个调虚表 +0xC = Clear）
  for (std::size_t i = 0; i < buckets_.size(); ++i) buckets_[i].clear();
  // (2) 13 张旧区类型表 delete 掉——这里由返回值持有，无需手工释放
  // (3) 区编号整表清 0（0x56C5A2）
  const int n = count();
  for (int i = 0; i < n; ++i) at(i).zone = 0;

  ZoneResult r;
  // 0 号槽先塞一个 7：区编号 0 表示"没有区"，占位用
  r.land_of.push_back(kLandOffMap);      // 0x56C5C5

  // (4) 线性扫全表，逐段洪泛
  std::uint16_t zone = 1;
  int biggest = -1;                      // local_28 / puVar14 初值 0xFFFFFFFF
  int run_len = 0;
  last_neighbour_ = 0;
  for (int p = 0; p < n;) {
    const std::uint8_t land = at(p).land;
    if (land == kLandOffMap || at(p).zone != 0) {
      ++p;
      continue;
    }
    last_neighbour_ = 0;                 // [0xABDE8C] = 0，每个新区重置
    const int painted = Flood(p, zone, &run_len);
    if (biggest < painted) {
      biggest = painted;
      r.largest_zone = zone;
    }
    r.land_of.push_back(land);
    ++zone;
    p += run_len;                        // ⚠️ 按"行段长度"跳，可能是 0 甚至 -1
  }
  r.zone_count = zone;                   // MapClass +0x4C = 区数 + 1

  // (5) 桥/隧道两端连通登记（0x56C6B0）。随机地图里 MapClass+0x60 == 0，
  //     整段空转，故这里不实现。

  // (6) 桶 -> CSR 邻接表
  const int zc = r.zone_count;
  std::vector<std::uint16_t> deg(static_cast<std::size_t>(zc), 0);
  for (int bi = 0; bi < 256; ++bi) {
    for (std::size_t k = 0; k < buckets_[bi].size(); ++k) {
      const std::uint32_t pr = buckets_[bi][k];
      ++deg[pr & 0xFFFF];
      ++deg[pr >> 16];
    }
  }
  std::vector<std::vector<std::uint16_t> > adj(static_cast<std::size_t>(zc));
  for (int z = 0; z < zc; ++z) adj[z].assign(deg[z], 0);
  for (int z = 0; z < zc; ++z) deg[z] = 0;
  for (int bi = 0; bi < 256; ++bi) {
    for (std::size_t k = 0; k < buckets_[bi].size(); ++k) {
      const std::uint32_t pr = buckets_[bi][k];
      const std::uint32_t a = pr & 0xFFFF;
      const std::uint32_t b = pr >> 16;
      // ⚠️ 两次写都排在两次自增之前（0x56C8B4-0x56C8CE）。a == b 时会自己盖自己，
      //    但登记谓词里有 `nz != zone`，所以不会发生。
      adj[a][deg[a]] = static_cast<std::uint16_t>(b);
      adj[b][deg[b]] = static_cast<std::uint16_t>(a);
      ++deg[a];
      ++deg[b];
    }
  }

  // (7) 13 种移动区，各建一张「区编号 -> 区类型」表
  std::vector<std::uint16_t> stack(static_cast<std::size_t>(zc), 0);
  r.zone_type.assign(kMovementZoneCount, std::vector<std::uint16_t>());
  for (int mz = 0; mz < kMovementZoneCount; ++mz) {
    std::vector<std::uint16_t>& t = r.zone_type[mz];
    t.assign(static_cast<std::size_t>(zc), 0);
    for (int z = 0; z < zc; ++z) {
      t[z] = static_cast<std::uint16_t>(kMoveTable[mz][r.land_of[z]] != 1);
    }
    std::uint16_t next = 2;              // 连通分量编号从 2 起
    for (int z = 0; z < zc; ++z) {
      if (t[z] != 0) continue;
      const int key = kMoveTable[mz][r.land_of[z]];   // 恒为 1
      int sp = 1;
      stack[0] = static_cast<std::uint16_t>(z);
      t[z] = next;
      do {
        --sp;
        const std::uint16_t u = stack[sp];
        int slot = sp;                   // ⚠️ 从刚弹出的槽位开始往后覆盖着写
        for (int k = static_cast<int>(deg[u]) - 1; k >= 0; --k) {  // 倒着扫邻居
          const std::uint16_t v = adj[u][k];
          if (kMoveTable[mz][r.land_of[v]] == key && t[v] == 0) {
            stack[slot] = v;
            ++sp;
            ++slot;
            t[v] = next;
          }
        }
      } while (sp != 0);
      ++next;
    }
    t[0] = 0xFFFF;                       // 0 号区永远"不可达"
  }
  return r;
}

std::uint16_t ZoneTable::GetMovementZoneType(const ZoneResult& r, int x, int y,
                                             int movement_zone) const {
  return r.zone_type[movement_zone][at(CellToIndex(x, y)).zone];
}

}  // namespace rmg
}  // namespace ra2
