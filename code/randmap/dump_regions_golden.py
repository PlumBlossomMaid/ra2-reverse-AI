# -*- coding: utf-8 -*-
u"""阶段 3「Init regions」四步流程的整函数模拟执行。

跑的是 0x598960 里两条调试日志之间那一整段的真身：
  工作记录 +0x38/+0x3C 清 -1 → CreateRegions 0x58CF90
  → 每个 wet 区域 Grow 0x58E740(4/5/6) + FloodRest 0x58E9B0
  → Finalize 0x58D010(0)
一个桩都不留：0x4865D0 / 0x4867B0 / 0x486380 三个谓词、0x5A0700 与 0x58D410
两个边界格函数、0x5657A0 的哑元退化全部是原身。

环境在 StageOracle 之上多摆四样：
  1) 摘掉 PaintOracle 对 0x4865D0 的「恒真」桩 —— 那个地址同时就是
     「这一格是水或岸」，区域起种全靠它，恒真的话整张图会被判成一个区域。
  2) [0xAA0748] ClearToGreenLat = 494（temperate）。PlaceOracle 的 THEATER
     表里没有它，而 0x4867B0 会拿它当 16 块过渡砖的起点；留 0 的话砖号
     0..15 会被误判成绿地。
  3) 全局区域向量 0xABDF90 按静态初始化器 0x58BC30 的原样摆好
     （虚表 0x7ED858、增长步长 10）。留全零的话增长步长是 0，
     DynamicVectorClass::push 会**静默失败**，区域一个都登记不上。
  4) MapSeedClass 用**静态实例 0xABDFD8** 而不是 bump 出来的临时对象。
     0x58CAD1 `mov ecx,0xABDFD8` 与 0x59664C `mov ecx,0xABDFD8; call 0x598960`
     实锤了流水线的 this 就是它。于是一批"来源不明"的全局当场对上号：
       +0x180/+0x184 → 0xABE158/0xABE15C 地图宽 W / 高 H（0x5A0700 的容量预留）
       +0x308        → 0xABE2E0 块编号计数器
       +0x30C        → 0xABE2E4 ⚠️ 就是 SeedRegion 并区时 Level 为 -1 的兜底值
       +0x318..324   → 0xABE2F0..FC 全图边界
       +0x8B8        → 0xABE890 Randomizer

另外把 malloc/free 换成**带回收的** bump 分配器：SeedRegion 每次起种都要一个
增长步长 50000 的栈向量（20 万字节），而原来的桩 free 是空操作，几百次起种
就把 64MB 的 scratch 吃光了。回收按请求字节数精确匹配，命中率接近 100%。

基准记：
  IN  行 —— 阶段 3 的**输入**（每格 tile / subtile / level 与工作记录 +0x4B）
  OUT 行 —— 终态（tile / subtile / level / 归属 +0x38 / 入队标记 +0x3C）
  REG 行 —— 区域表（id / 格数 / 层 / wet / 种子 / +0x1A）
  外加阶段 3 自己的随机数调用次数与共享哑元坐标 [0xABDC74]
"""
import struct
import sys

from dump_stage_water_golden import StageOracle, GREEN
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_STAGE2 = 0x59A6C0
A_CREATE = 0x58CF90
A_GROW = 0x58E740
A_FLOOD = 0x58E9B0
A_FINALIZE = 0x58D010

CLEAR_TO_GREEN = 494      # temperate ClearToGreenLat（tileset_starts.txt）
SEEDOBJ = 0xABDFD8        # MapSeedClass::Instance
REGVEC = 0xABDF90         # DynamicVectorClass<RegionClass*>，内联在 .bss 里


def sx(v):
    return v - (1 << 32) if v >> 31 else v


class RegionOracle(StageOracle):
    def __init__(self, w, h, seed, players, water, map_type, usable,
                 profile="mix"):
        self.pool = {}       # 请求字节数 -> 可复用的块
        self.owned = {}      # 块地址 -> 请求字节数
        StageOracle.__init__(self, w, h, seed, players, water, map_type,
                             usable, profile)
        self.uc.hook_del(self.h_paintable)
        self.wr32(0xAA0748, CLEAR_TO_GREEN)

        # 静态初始化器 0x58BC30 的原样（跳过它末尾的 atexit 登记）
        self.wr32(REGVEC + 0x00, 0x7ED858)
        self.wr32(REGVEC + 0x04, 0)
        self.wr32(REGVEC + 0x08, 0)
        self.uc.mem_write(REGVEC + 0x0C, b"\x01\x00")
        self.wr32(REGVEC + 0x10, 0)
        self.wr32(REGVEC + 0x14, 10)

        # MapSeedClass 换成静态实例：先整体清零，再摆字段。
        # 0xABE2F0..FC（= +0x318）与 0xABE890（= +0x8B8）在这段之外，不受影响。
        for off in range(0, 0x310, 4):
            self.wr32(SEEDOBJ + off, 0)
        self.seedobj = SEEDOBJ
        self.wr32(SEEDOBJ + 0x3C, map_type)
        self.wr32(SEEDOBJ + 0x4C, water)
        self.wr32(SEEDOBJ + 0x50, players)
        self.wr32(SEEDOBJ + 0x180, w)
        self.wr32(SEEDOBJ + 0x184, h)
        self.wr32(SEEDOBJ + 0x308, 0)
        self.wr32(SEEDOBJ + 0x30C, 4)
        self.map_type = map_type

    # ---- 带回收的分配器（只在本 oracle 里替换，不动别的基准） ----
    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        free = self.pool.get(n)
        if free:
            p = free.pop()
            uc.mem_write(p, b"\0" * n)
        else:
            p = self._bump(n)
        self.owned[p] = n
        uc.reg_write(UC_X86_REG_EAX, p)
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        p = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        n = self.owned.pop(p, None)
        if n:
            self.pool.setdefault(n, []).append(p)
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    # ---- 地图输入 ----
    def stage2(self):
        u"""跑阶段 2 拿一张真实地形（含岸砖与层高）。"""
        self.rng_calls = 0
        self._call(A_STAGE2, self.seedobj)
        return self.rng_calls

    def synth(self, kind):
        u"""合成地形。tile 只用 1 时全图都不是水/岸/绿，CreateRegions 一个区域
        都建不出来 —— 这是 id == 0 那条唯一取数分支的**唯一**入口。"""
        for (x, y) in self._diamond():
            if kind == "dry-flat":
                tile, lvl = 1, 0
            elif kind == "dry-bands":
                tile, lvl = 1, ((x + y) // 6) % 4
            elif kind == "dry-checker":
                tile, lvl = 1, (x // 3 + y // 3) % 3
            elif kind == "dry-blocks":
                # 4x4 的层高方块：每块连通区十几格，既不足 75 又不是
                # 孤点——这才能验出 0x5A0700 那个 break（一格只入表一次）
                tile = 1
                lvl = (x // 4 + y // 4) % 4
            elif kind == "dry-tiles":
                # 2x2 的层高磁砖 + 小图：菱形里每一块同层连通区都是
                # 四格左右，四周周围的层高各不相同——选中哪一格直接
                # 决定 Reassign 写回去的是哪个层高
                tile = 1
                lvl = ((x // 2) * 3 + (y // 2)) % 5
            elif kind == "dry-noise":
                # 层高逐格跳：每一块同层连通区都很小，
                # Finalize 会一路撞进"不足 75 格"那三条分支
                tile = 1
                lvl = ((x * 7 + y * 13) // 5) % 5
            elif kind == "green-bands":
                tile = GREEN if (x + y) % 3 else 1
                lvl = ((x + y) // 8) % 3
            elif kind == "lake-wall":
                # 水泡 + 一圈砌死的 tile 1：Grow 先吃掉两圈未定砖，
                # 到第三圈碍上墙 → 当场放弃，那一圈剩下的邻格再也不会被吸
                d = abs(x - (self.w + self.h // 2)) + abs(y - (self.h + self.w // 2))
                tile = WATER if d < 4 else (1 if 6 <= d <= 7 else UNDECIDED)
                lvl = 0
            elif kind == "lake-step":
                # 同上，但墙换成层高台阶：Grow 的"只吃同层"那条才是拦路的
                d = abs(x - (self.w + self.h // 2)) + abs(y - (self.h + self.w // 2))
                tile = WATER if d < 4 else UNDECIDED
                lvl = 0 if d <= 6 else 2
            elif kind == "water-lake":
                inside = abs(x - (self.w + self.h // 2)) + \
                    abs(y - (self.h + self.w // 2)) < 6
                tile = WATER if inside else UNDECIDED
                lvl = 0
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)

    def set_flag4b(self, mode):
        for i in range(self.side * self.side):
            r = self.work + i * WORK_SIZE
            if mode == "all":
                v = 1
            elif mode == "none":
                v = 0
            else:
                v = 1 if (i % 3) else 0
            self.uc.mem_write(r + 0x4B, bytes([v]))

    # ---- 阶段 3 ----
    def snap_in(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           self.uc.mem_read(
                               self.work + (y * self.side + x) * WORK_SIZE
                               + 0x4B, 1)[0])
        return out

    def snap_out(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)))
        return out

    def regions(self):
        out = []
        data = self.rd32(REGVEC + 0x04)
        for i in range(self.rd32(REGVEC + 0x10)):
            r = self.rd32(data + i * 4)
            sc = self.rd32(r + 0x16)
            out.append((sx(self.rd32(r + 0x08)), sx(self.rd32(r + 0x0C)),
                        sx(self.rd32(r + 0x10)),
                        self.uc.mem_read(r + 0x14, 1)[0],
                        sc & 0xFFFF, (sc >> 16) & 0xFFFF,
                        self.uc.mem_read(r + 0x1A, 1)[0]))
        return out

    def init_regions(self):
        u"""0x598960 的阶段 3 段落，逐条照抄。"""
        self.rng_calls = 0
        # 1) 拆旧区域 + 两个字段清 -1
        for i in range(self.rd32(REGVEC + 0x10) - 1, -1, -1):
            p = self.rd32(self.rd32(REGVEC + 0x04) + i * 4)
            if p:
                self._call(0x5AC290, p, (1,))
        self.wr32(0xABED14, 0)
        for i in range(self.side * self.side):
            r = self.work + i * WORK_SIZE
            self.wr32(r + 0x38, 0xFFFFFFFF)
            self.wr32(r + 0x3C, 0xFFFFFFFF)
        # 2) 建区域
        self._call(A_CREATE, 0)
        # 3) 每个 wet 区域先长几圈再漫灌；上界每轮重读
        i = 0
        while i < self.rd32(REGVEC + 0x10):
            r = self.rd32(self.rd32(REGVEC + 0x04) + i * 4)
            if self.uc.mem_read(r + 0x14, 1)[0] != 0:
                big = self.map_type in (3, 4)
                rings = (1 if sx(self.rd32(r + 0x0C)) > 8000 else 0) \
                    + (0 if big else 1) + 4
                self._call(A_GROW, r, (rings,))
                self._call(A_FLOOD, r)
            i += 1
        # 4) 兜底
        self._call(A_FINALIZE, 0)
        return self.rng_calls


# 名字, W, H, 种子, 玩家, WaterAmount, MapType, 可用区, 剖面, 地形, +0x4B
CASES = [
    # ── 阶段 2 出来的真实地形 ──
    ("s2-mt1-p2", 10, 10, 0x12345678, 2, 0, 1, (0, 0, 10, 10), "mix",
     "stage2", None),
    ("s2-mt1-p4", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "mix",
     "stage2", None),
    ("s2-mt0-p2", 20, 20, 0x12345678, 2, 0, 0, (0, 0, 20, 20), "mix",
     "stage2", None),
    ("s2-mt2-p4", 12, 10, 0x0F0F0F0F, 4, 20, 2, (0, 0, 12, 10), "mix",
     "stage2", None),
    ("s2-mt1-real", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "real",
     "stage2", None),
    # MapType 3：Grow 少长一圈（4 圈而不是 5 圈）
    ("s2-mt3-rings4", 12, 10, 0x1234, 2, 60, 3, (0, 0, 12, 10), "mix",
     "stage2", None),
    # ── 合成地形：走那些阶段 2 到不了的分支 ──
    # 全图都不是水/岸/绿 → CreateRegions 建不出区域 → Finalize 里 id == 0
    # 且层高有落差 → **唯一的取数点 0x58CAF9**
    ("syn-dry-bands", 8, 8, 0x11111111, 2, 0, 1, (0, 0, 8, 8), "mix",
     "dry-bands", "mixed"),
    ("syn-dry-checker", 8, 8, 0x22222222, 2, 0, 1, (0, 0, 8, 8), "mix",
     "dry-checker", "mixed"),
    # 6x6：菱形里只有 66 格，一整块都不足 75 → 必走"小块"分支
    ("syn-dry-tiny", 6, 6, 0x33333333, 2, 0, 1, (0, 0, 6, 6), "mix",
     "dry-bands", "all"),
    ("syn-dry-noise-a", 10, 10, 0x0000002A, 2, 0, 1, (0, 0, 10, 10), "mix",
     "dry-noise", "mixed"),
    ("syn-dry-noise-b", 10, 10, 0xFFFFFFFF, 2, 0, 1, (0, 0, 10, 10), "mix",
     "dry-noise", "all"),
    ("syn-dry-noise-c", 9, 11, 0x13579BDF, 2, 0, 1, (0, 0, 9, 11), "mix",
     "dry-noise", "none"),
    ("syn-dry-blocks", 16, 16, 0x0BADC0DE, 2, 0, 1, (0, 0, 16, 16), "mix",
     "dry-blocks", "mixed"),
    ("syn-dry-blocks-b", 14, 18, 0x0000BEEF, 2, 0, 1, (0, 0, 14, 18), "mix",
     "dry-blocks", "all"),
    ("syn-dry-tiles-a", 7, 7, 0x0000CAFE, 2, 0, 1, (0, 0, 7, 7), "mix",
     "dry-tiles", "mixed"),
    ("syn-dry-tiles-b", 6, 6, 0xFACEB00C, 2, 0, 1, (0, 0, 6, 6), "mix",
     "dry-tiles", "all"),
    ("syn-dry-tiles-c", 11, 9, 0x2468ACE0, 2, 0, 1, (0, 0, 11, 9), "mix",
     "dry-tiles", "none"),
    # 全平：8 个方向都找不到层高不同的邻格 → 抽完数照样落回"建区域"
    ("syn-dry-flat", 8, 8, 0x44444444, 2, 0, 1, (0, 0, 8, 8), "mix",
     "dry-flat", "none"),
    ("syn-dry-flat-big", 14, 14, 0x55555555, 2, 0, 1, (0, 0, 14, 14), "mix",
     "dry-flat", "all"),
    # 绿地条带：wet 区域一堆，Grow 会把中间的干格子吃掉
    ("syn-green-bands", 10, 10, 0x66666666, 2, 0, 1, (0, 0, 10, 10), "mix",
     "green-bands", "mixed"),
    ("syn-green-none4b", 10, 10, 0x77777777, 2, 0, 1, (0, 0, 10, 10), "mix",
     "green-bands", "none"),
    # 一片未定砖里挖个水湖：Grow 能一路长满全图
    ("syn-water-lake", 10, 10, 0x88888888, 2, 0, 1, (0, 0, 10, 10), "mix",
     "water-lake", "all"),
    ("syn-water-lake-mt3", 10, 10, 0x99999999, 2, 0, 3, (0, 0, 10, 10), "mix",
     "water-lake", "all"),
    # Grow 中途放弃：墙前那一圈只能被吸进一部分
    ("syn-lake-wall", 12, 12, 0xAAAAAAAA, 2, 0, 1, (0, 0, 12, 12), "mix",
     "lake-wall", "none"),
    ("syn-lake-wall-4b", 12, 12, 0xBBBBBBBB, 2, 0, 1, (0, 0, 12, 12), "mix",
     "lake-wall", "all"),
    ("syn-lake-step", 12, 12, 0xCCCCCCCC, 2, 0, 1, (0, 0, 12, 12), "mix",
     "lake-step", "mixed"),
]

HEAD = (u"# 阶段 3 Init regions 整函数模拟"
        u"执行基准\n"
        u"# 生成脚本 code/randmap/dump_regions_golden.py\n"
        u"# IN  x y tile subtile level flag4b\n"
        u"# OUT x y tile subtile level owner queued\n"
        u"# REG id cellCount level wet seedX seedY finalized\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493 ClearToGreenLat=494"
        u"（temperate）\n")


def main():
    out = []
    for (name, w, h, seed, players, water, mt, usable, profile, terrain,
         f4b) in CASES:
        o = RegionOracle(w, h, seed, players, water, mt, usable, profile)
        if terrain == "stage2":
            o.stage2()
        else:
            o.synth(terrain)
        if f4b is not None:
            o.set_flag4b(f4b)
        before = o.snap_in()
        calls = o.init_regions()
        after = o.snap_out()
        regs = o.regions()
        dummy = o.rd32(0xABDC50 + 0x24)
        head = ("CASE %s %d %d seed=0x%08X players=%d water=%d mt=%d"
                " usable=%d,%d,%d,%d tiles=%s terrain=%s flag4b=%s"
                " -> rng=%d regions=%d nextid=%d dummy=%d,%d"
                % (name, w, h, seed, players, water, mt, usable[0], usable[1],
                   usable[2], usable[3], profile, terrain, f4b, calls,
                   len(regs), sx(o.rd32(0xABED14)),
                   dummy & 0xFFFF, (dummy >> 16) & 0xFFFF))
        out.append(head)
        for (x, y) in sorted(before):
            t, st, lv, fl = before[(x, y)]
            out.append("  IN %d %d %d %d %d %d" % (x, y, t, st, lv, fl))
        for (x, y) in sorted(after):
            t, st, lv, ow, q = after[(x, y)]
            out.append("  OUT %d %d %d %d %d %d %d" % (x, y, t, st, lv, ow, q))
        for (rid, cnt, lv, wet, sxx, syy, fin) in regs:
            out.append("  REG %d %d %d %d %d %d %d"
                       % (rid, cnt, lv, wet, sxx, syy, fin))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/regions_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
