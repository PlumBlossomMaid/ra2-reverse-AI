// 悬崖相关的四组函数——阶段 4「Making regions」的收尾动作。
//
// 取证（gamemd.exe）：详见 docs/random-map/stage-making-regions.md §六 ~ §八
// 与 memory/data/decomp/rmg_retile_stage4{p,q,r}.txt。
#include "cliff_work.h"

#include <algorithm>
#include <cstdint>

#include "place_tile.h"   // IsUndecidedTile / IsProtectedTerrainCell / StampTile
#include "scaled_random.h"   // PickRange
#include "zones.h"        // IsWithinUsableArea

namespace ra2 {
namespace rmg {
namespace {

// 0x89F688：0=N 1=NE 2=E 3=SE 4=S 5=SW 6=W 7=NW。
const int kDeltaX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int kDeltaY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

inline CellCoord Step(const CellCoord& c, int dir) {
  return CellCoord{static_cast<std::int16_t>(c.x + kDeltaX[dir]),
                   static_cast<std::int16_t>(c.y + kDeltaY[dir])};
}

inline std::size_t WorkIdx(const RegionContext& ctx, const CellCoord& c) {
  const long long i =
      static_cast<long long>(c.y) * ctx.grid.side() + c.x;
  if (i < 0) return 0;
  const long long n = static_cast<long long>(ctx.work->size());
  return static_cast<std::size_t>(i >= n ? n - 1 : i);
}

// 0x5A00C0 GetWorkOwner：work 为空返回 -1。
inline int Owner(const RegionContext& ctx, const CellCoord& c) {
  if (ctx.work == nullptr) return -1;
  return (*ctx.work)[WorkIdx(ctx, c)].blob_id;
}

// 0x5A0090 SetWorkOwner：work 为空静默无操作。
inline void SetOwner(RegionContext& ctx, const CellCoord& c, int v) {
  if (ctx.work == nullptr) return;
  (*ctx.work)[WorkIdx(ctx, c)].blob_id = v;
}

// 0x5A6C10 压砖器（RerollMultitileCliffs 的 StampTile 用）。level == -1 时
// 层高写入被跳过，但 +0x11C 坡度照写——与 BuildRamp 那边同一个入口。
inline void StampByIndex(MakingWorld& world, const MapGrid& grid, int index,
                         const CellCoord& at, int blob_id, int level) {
  const TileType* t = world.TileByIndex(index);
  if (t == nullptr) return;
  StampTile(world, grid, *t, at, blob_id, level);
}

}  // namespace

// ───────────────────────── 0x579010 FillNotch ─────────────────────────

bool FillNotch(RegionContext& ctx, MakingWorld& world, const CellCoord& c,
               int owner) {
  // 取数点：无。0x579B70 高差掩码，位定义见 HigherMask。
  const unsigned m = HigherMask(ctx, world, c);
  bool hit = false;

  // 第一组：两个相对的正方向都比我高（0x88 = N+S、0x22 = E+W）。
  if ((m & 0xA0) == 0xA0 && (m & 0x11) == 0x11) hit = true;   // 0x5A_101
  // 第四组（代码里排在前面）：一侧直接高、另一侧"隔壁的隔壁"高。
  // 看的是**邻格的掩码**，不是本格的。
  if ((m & 0x20) != 0 && (HigherMask(ctx, world, Step(c, 2)) & 0x02) != 0)
    hit = true;                              // W 高，看 E 邻格的 E 位
  if ((m & 0x02) != 0 && (HigherMask(ctx, world, Step(c, 6)) & 0x20) != 0)
    hit = true;                              // E 高，看 W 邻格的 W 位
  if ((m & 0x08) != 0 && (HigherMask(ctx, world, Step(c, 0)) & 0x80) != 0)
    hit = true;                              // S 高，看 N 邻格的 N 位
  if ((m & 0x80) != 0 && (HigherMask(ctx, world, Step(c, 4)) & 0x08) != 0)
    hit = true;                              // N 高，看 S 邻格的 S 位

  // 第二组：对角对带例外（两条例外是"规规矩矩的直角外拐角"，交给 0x5A19E0）。
  if ((m & 0x11) == 0x11 &&                 // NE + SW
      ((m & 0x0A) != 0x0A || (m & 0xE0) != 0) &&
      ((m & 0xA0) != 0xA0 || (m & 0x0E) != 0)) {
    hit = true;
  }
  if ((m & 0x44) == 0x44 &&                 // SE + NW
      ((m & 0x28) != 0x28 || (m & 0x83) != 0) &&
      ((m & 0x82) != 0x82 || (m & 0x38) != 0)) {
    hit = true;
  }

  // 第三组：隔一个空当的八对（无例外，命中即抬）。
  if ((m & 0x2C) == 0x24 || (m & 0xA1) == 0x21 || (m & 0x1A) == 0x12 ||
      (m & 0xC2) == 0x42 || (m & 0x0B) == 0x09 || (m & 0x68) == 0x48 ||
      (m & 0x86) == 0x84 || (m & 0xB0) == 0x90) {
    hit = true;
  }

  // 最后汇总：N+S 槽 / E+W 槽 / 上面任一判据。
  if (!((m & 0x88) == 0x88 || (m & 0x22) == 0x22 || hit)) return true;

  // 0x5792A9 的 edi 读的是 [esp+0x28]——GetWorkOwner（ret 4）返回后 esp 已
  // 恢复到 push ecx 之前，该槽指向调用者 param2，不是 0x5792A0 写入的 packed
  // （Westwood 栈偏移错位，false 出口的 packed 判定实际变成 param2 判定）。
  // 判定：blob<=0 → 抬高；blob==param2 → 抬高；param2==-1 → 抬高；否则 false。
  // 因此调用者恒传 -1（Rebuild 0x58EF49 / RepaintAll 0x578E60 的 param_2
  // 都是 0xFFFFFFFF）→ 判据命中恒抬高，false 出口是死代码。
  const int o = Owner(ctx, c);
  if (o > 0 && o != owner && owner != -1) return false;

  // byte 上的 +4，会回绕。work 为空时层高照改、只不写归属。
  world.SetLevel(c, static_cast<std::int8_t>(world.Level(c) + 4));
  SetOwner(ctx, c, owner);   // 0x5A0090(&coord, param2)
  for (int d = 0; d < 8; ++d) {
    // 递归参数 = param2（0x5792EE push edi），返回值丢弃（0x579301）。
    FillNotch(ctx, world, Step(c, d), owner);
  }
  return true;
}

// ───────────────────────── 0x579620 AutoCliff ─────────────────────────

bool AutoCliff(RegionContext& ctx, MakingWorld& world, const CellCoord& c,
               int owner) {
  // 入口每格取一个 r = PickRange(0, 5)，同一个 r 被 r%3 和 r&1 双份消费。
  const int r = PickRange(*ctx.rng, 0, 5);          // 0x598030 / 0x59805E
  const unsigned m = HigherMask(ctx, world, c);      // 0x579B70

  // m(dx,dy)：邻格的掩码（递归调 HigherMask）。
  const auto nm = [&](int dx, int dy) {
    return HigherMask(ctx, world,
                      CellCoord{static_cast<std::int16_t>(c.x + dx),
                                static_cast<std::int16_t>(c.y + dy)});
  };

  int idx = -1;   // 1 基的组内下标；砖号 = CliffSet + idx - 1
  if ((m & 0xA0) == 0xA0) {                          // W + N
    idx = (nm(0, +1) & 0x20) != 0 || (nm(+1, 0) & 0x80) != 0 ? 0x22
                                                            : r % 3 + 9;
  } else if ((m & 0x82) == 0x82) {                   // E + N
    idx = 0x27;
  } else if ((m & 0x0A) == 0x0A) {                   // E + S
    idx = 0x21;
  } else if ((m & 0x28) == 0x28) {                   // S + W
    idx = 0x28;
  } else if (m & 0x02) {                             // E 高
    idx = (!(m & 0x04) || (m & 0x18) ||
           (nm(0, +1) & 0x0A) == 0x0A)
              ? ((!(m & 0x0C) && !(nm(+1, +1) & 0x02) && (r & 1)) ? 1 : 0x26)
              : r % 3 + 0x23;
  } else if (m & 0x20) {                             // W 高
    idx = (!(m & 0x10) || (m & 0x0C) ||
           (nm(0, +1) & 0x28) == 0x28)
              ? 0x12
              : r % 3 + 0x0F;
  } else if (m & 0x08) {                             // S 高
    idx = (!(m & 0x04) || (m & 0x03) ||
           (nm(+1, 0) & 0x0A) == 0x0A)
              ? ((!(m & 0x06) && !(nm(+1, +1) & 0x08) && (r & 1)) ? 0x15 : 0x1A)
              : r % 3 + 0x17;
  } else if (m & 0x80) {                             // N 高
    // 嵌套 cell = E 邻格 (X+1,Y)：0x579A04 `lea ecx,[eax+1]`（eax=[ebp+0x24]
    // 打包坐标，+1 即 X+1）铁证——不是 S 邻格（此前 nm(0,+1) 为错，导致
    // (57,57) 处 idx=5 贴 tile=53 而原版 idx=8 贴 tile=56）。
    idx = (!(m & 0x01) || (m & 0x06) ||
           (nm(+1, 0) & 0x82) == 0x82)
              ? 8
              : r % 3 + 5;
  } else if (m & 0x01) {                             // 只有 NE
    idx = 2;
  } else if (m & 0x04) {                             // 只有 SE
    idx = (r & 1) + 0x1D;
  } else if (m & 0x10) {                             // 只有 SW
    idx = 0x16;
  } else if (m & 0x40) {                             // 只有 NW
    idx = r % 3 + 0x0C;
  } else {
    return true;                                     // 掩码全 0：什么也不贴
  }

  if (idx < 1) return true;                          // 兜底（实际取不到）
  const int cliff_set = ctx.theater->cliff_set;      // [0xAA1020]
  const TileType* t = world.TileByIndex(cliff_set + idx - 1);
  if (t == nullptr) return true;
  // 落点偏移表 0xABDDA4 恒为 (0,0)（初始化函数 0x579320 没人调、唯一非零项
  // 挂在选不中的下标 32 上），贴砖永远贴在本格上，表直接省掉。
  bool ok = true;
  PlaceTileParams p;
  p.allow_lo = 0;
  p.allow_hi = 0;
  p.base_level = world.Level(c);
  p.blob_id = owner;
  p.ok = &ok;
  p.no_blob_tracking = false;
  const bool placed =
      PlaceTile(world, ctx.grid, *ctx.theater, *t, c, p);   // 0x57B440
  // 出口判据 `!placed && !ok` 两个都假才中断 0x578E60 的第二遍。
  if (!placed && !ok) return false;
  return true;
}

// ───────────────────────── 0x5A19E0 TrimCliffCorners ─────────────────────────

void TrimCliffCorners(RegionContext& ctx, MakingWorld& world) {
  // 全图迭代：五个邻格 + 高差掩码。⚠️ 分支**排他**（0x5A1A4B cmp/jne 链）：
  // 入口先判本格掩码 m == 0x83/0x38/0xE0，掩码决定走哪条；分支内附加条件
  // 失败直接跳本格结尾（0x5A1C49），**不再试下一条**。硬币只在附加条件全过
  // 之后才抽（0x5A1A7B/0x5A1B2C）。
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    const CellCoord n5 = Step(c, 5);   // SW
    const CellCoord n1 = Step(c, 1);   // NE
    const CellCoord n3 = Step(c, 3);   // SE
    const CellCoord n2 = Step(c, 2);   // E
    const CellCoord n6 = Step(c, 6);   // W
    const unsigned m = HigherMask(ctx, world, c);

    if (m == 0x83) {                   // N+NE+E 的直角，削 E 邻格
      if (HigherMask(ctx, world, n3) == 0x83 &&
          IsUndecidedTile(world.IsoTile(n2)) &&
          PickBelow(*ctx.rng, 2) == 0) {
        world.SetLevel(n2, static_cast<std::int8_t>(world.Level(n2) - 4));
        SetOwner(ctx, n2, Owner(ctx, c));
      }
    } else if (m == 0x38) {            // S+SW+W 的直角，削 W 邻格
      if (HigherMask(ctx, world, n3) == 0x38 &&
          IsUndecidedTile(world.IsoTile(n6)) &&
          PickBelow(*ctx.rng, 2) == 0) {
        world.SetLevel(n6, static_cast<std::int8_t>(world.Level(n6) - 4));
        SetOwner(ctx, n6, Owner(ctx, c));
      }
    } else if (m == 0xE0) {            // W+NW+N 的直角，垫本格（不抛硬币）
      if (HigherMask(ctx, world, n5) == 0xE0 &&
          HigherMask(ctx, world, n1) == 0xE0 &&
          IsUndecidedTile(world.IsoTile(c))) {
        world.SetLevel(c, static_cast<std::int8_t>(world.Level(c) + 4));
        SetOwner(ctx, c, Owner(ctx, n6));
      }
    }
  });
}

// ───────────────────────── 0x5A1350 RerollCliff ─────────────────────────

int RerollCliff(Randomizer& rng, int tile, int cliff_set) {
  const int rel = tile - cliff_set;   // 0..39
  int out = rel;
  // 六个三连组 {4,5,6} {8,9,10} {11,12,13} {14,15,16} {22,23,24} {34,35,36}。
  // 组内第 1 → 第 2/3、第 2 → 第 1/3、第 3 → 第 1/2。
  // 前五组内联：`fadd 1.0` 把下界顶上去（与 PickRange 的两步乘**不是同一条
  // 浮点路径**）；第六组走外联 PickRange。
  switch (rel) {
    case 4: {  // 组内第 1：fadd 1.0 顶下界，拒绝 > 2 → {1,2}，+4 → {5,6}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                     kInvUintMax * 2.0 +
                 1.0);
      } while (e > 2);
      out = e + 4;
      break;
    }
    case 5: {  // 组内第 2：{0,1} 之后 ×2 跳过自己 → {4,6}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + e + 4;
      break;
    }
    case 6: {  // 组内第 3：{0,1} 直接用 → {4,5}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + 4;
      break;
    }
    case 8: {  // 组 {8,9,10} 第 1 → {9,10}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                     kInvUintMax * 2.0 +
                 1.0);
      } while (e > 2);
      out = e + 8;
      break;
    }
    case 9: {  // 第 2 → {8,10}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + e + 8;
      break;
    }
    case 10: {  // 第 3 → {8,9}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + 8;
      break;
    }
    case 11: {  // 组 {11,12,13} 第 1 → {12,13}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                     kInvUintMax * 2.0 +
                 1.0);
      } while (e > 2);
      out = e + 11;
      break;
    }
    case 12: {  // 第 2 → {11,13}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + e + 11;
      break;
    }
    case 13: {  // 第 3 → {11,12}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + 11;
      break;
    }
    case 14: {  // 组 {14,15,16} 第 1 → {15,16}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                     kInvUintMax * 2.0 +
                 1.0);
      } while (e > 2);
      out = e + 14;
      break;
    }
    case 15: {  // 第 2 → {14,16}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + e + 14;
      break;
    }
    case 16: {  // 第 3 → {14,15}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + 14;
      break;
    }
    case 22: {  // 组 {22,23,24} 第 1 → {23,24}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                     kInvUintMax * 2.0 +
                 1.0);
      } while (e > 2);
      out = e + 22;
      break;
    }
    case 23: {  // 第 2 → {22,24}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + e + 22;
      break;
    }
    case 24: {  // 第 3 → {22,23}
      int e;
      do {
        e = Ftol(static_cast<double>(static_cast<std::uint32_t>(rng.Random())) *
                 kInvUintMax * 2.0);
      } while (e > 1);
      out = e + 22;
      break;
    }
    case 34:  // 组 {34,35,36} 走外联 PickRange(1,2) → {35,36}
      out = PickRange(rng, 1, 2) + 0x22;
      break;
    case 35: {  // PickRange(0,1) 后 `lea eax,[eax+eax+0x22]`（同一个 e 用两次，只抽一次）
      const int e = PickRange(rng, 0, 1);
      out = e + e + 0x22;
      break;
    }
    case 36:  // PickRange(0,1) → {34,35}
      out = PickRange(rng, 0, 1) + 0x22;
      break;
    case 28:  // 常数对换，不取随机数
      out = 29;
      break;
    case 29:
      out = 28;
      break;
    default:
      break;   // 原样返回（含 rel < 4、rel > 36）
  }
  return cliff_set + out;
}

// ───────────────────────── 0x5A17F0 RerollMultitileCliffs ─────────────────────────

void RerollMultitileCliffs(RegionContext& ctx, MakingWorld& world) {
  const int cliff_set = ctx.theater->cliff_set;   // [0xAA1020]
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    const int tile = world.IsoTile(c);
    if (tile < cliff_set || tile >= cliff_set + 0x28) return;   // 0x5A17F0 入口
    const CellCoord s = Step(c, 4);   // S
    const CellCoord e = Step(c, 2);   // E
    const CellCoord nbs[2] = {s, e};
    for (int k = 0; k < 2; ++k) {
      const CellCoord nb = nbs[k];
      if (world.IsoTile(nb) != tile) continue;
      // 无符号比较（子块号恒非负，int 等价）。
      if (!(world.SubTile(nb) < world.SubTile(c))) continue;
      const int alt = RerollCliff(*ctx.rng, tile, cliff_set);   // 0x5A1350
      if (alt == tile) continue;   // 只有"原样返回"那一路会相等
      const TileType* t = world.TileByIndex(tile);
      if (t == nullptr) continue;
      const int w = t->width;      // +0x2E4
      // 原点由邻格（子块号更小的那个）反推。
      const CellCoord origin{
          static_cast<std::int16_t>(nb.x - world.SubTile(nb) % w),
          static_cast<std::int16_t>(nb.y - world.SubTile(nb) / w)};
      const int blob = Owner(ctx, origin);   // work 为空 → -1
      StampByIndex(world, ctx.grid, alt, origin, blob, -1);
    }
  });
}

// ───────────────────────── 0x578E60 RepaintAll ─────────────────────────

void RepaintAll(RegionContext& ctx, MakingWorld& world) {
  // 工作数组为空就 new（阶段 1 已建好，这里只在独立调用时触发）。
  if (ctx.work != nullptr) {
    for (std::size_t i = 0; i < ctx.work->size(); ++i) {
      (*ctx.work)[i].flag4a = true;   // +0x4A 置 1（0x58C2C0 按下标取记录）
    }
  }
  // 第一遍：全图 FillNotch(-1)，false 提前终止。原版 0x578E60 的 param_2
  // 由调用者（0x598D73 push -1）给 0xFFFFFFFF，FillNotch/AutoCliff 的
  // owner 恒为 -1（0x578F2F mov edi,[esp+0x20] 读调用者栈第二参数）。
  // ⚠️ emu run_call 顺序压栈会把参数弄反（[0,0xFFFFFFFF] → owner=0），
  // golden 基准的 dump 脚本用 reversed 压栈 → owner=-1，与真实一致。
  bool keep = true;
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    if (!keep) return;
    const bool ok = FillNotch(ctx, world, c, -1);
    if (!ok) keep = false;
  });
  // 第二遍：只对非坡（+0x11C == 0）且未定砖的格子调 AutoCliff(-1)。
  // 原版 0x578F91 test bl,bl 沿用第一遍的 ok，两遍之间不复位。
  ctx.grid.ForEachCell([&](const CellCoord& c) {
    if (!keep) return;
    if (world.Ramp(c) != 0) return;
    if (!IsUndecidedTile(world.IsoTile(c))) return;
    if (!AutoCliff(ctx, world, c, -1)) keep = false;
  });
  // 贴砖器那对暂存全局（[0x880990] / [0x88098C]）在 C++ 侧无对应物。
}

}  // namespace rmg
}  // namespace ra2
