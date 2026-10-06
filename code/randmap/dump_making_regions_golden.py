# -*- coding: utf-8 -*-
u"""阶段 4「Making regions」五入口整链的模拟执行基准。

跑的是 0x598960 里 `RMG: Making regions` 日志之后那五个调用（一条桩都不打）：

    FUN_0058ebc0()         重算成员/包围盒；过大的区域切开（0x58D620 真身）
    FUN_0058ef10()         推倒重建区域（含 FillNotch 第一遍 + SeedRegion
                            + 邻区表 0x58F0C0 + 开通路 0x5905D0）
    FUN_005a19e0()         悬崖直角削垫（TrimCliffCorners 真身）
    FUN_00578e60(0,-1)     全图重刷砖（FillNotch 第二遍 + AutoCliff 每格
                           PickRange(0,5) + 收尾清 [0x880990]/[0x88098C]）
    FUN_005a17f0()         多格悬崖砖重掷（RerollCliff 真身 + StampTile 真身）

输入是**阶段 3 之后的完整状态**：区域表 + work.regionId/queued + 每格
tile/subtile/level。所以每个用例先跑真身 stage2（0x59A6C0）拿真实地形，
再跑 RegionOracle.init_regions（阶段 3 四步），然后才进阶段 4。

环境在 RegionOracle 之上再摆四样：
  1) [0xABE048] = RegionSize（MapSeedClass +0x70，切分阈值公式的输入）
  2) [0xABE044] = Accessibility（MapSeedClass +0x6C，0x5905D0 开坡道的概率）
  3) [0xA8ED2C] 砖表实体化 —— AutoCliff / StampTile / 0x5A17F0 都要按砖号
     索引解引用，且读几何（+0x2E4 宽 / +0x2E8 高 / 子块 +0x28 高 / +0x2A 坡）。
     实体化 CliffSet 40 块 + SlopeSetPieces 10 块 + PavedRoads 21 块 +
     PavedRoadEnds 4 块。砖号用合成值（温和气候里 TEMPERAT.INI 在 mix 内，
     E:\\YRLauncher 无此文件）：CliffSet=39（真实值）、SlopeSetPieces=200、
     PavedRoads=300、PavedRoadEnds=340 —— 与 C++ 侧 test_making_regions.cpp
     的 TheaterData 必须一致。
  4) [0x880990]/[0x88098C] 清零（0x578E60 收尾自己会清；入口清一遍保证
     前面函数不读脏值）。

基准记：五步各自的随机数消耗、终态每格 (tile, subtile, level, ramp,
overlay, overlayData) + work (owner, queued)、终态区域表、[0xABED14] 下一个
区域 id、[0xABDC74] 共享哑元坐标、0x58EF10 的返回值。
"""
import struct
import sys

from dump_regions_golden import RegionOracle, SEEDOBJ
import dump_blob_paint_golden as _blob

# 0x58D620 的扩散向量每轮增长 100000 个元素（400KB/次），几十次切分累积
# 数百 MB——64MB 的默认 scratch 是模拟器限制（原版真机堆无限）。
# 这里是本脚本私有修改（模块单例，独立进程），不污染其他基准。
_blob.SCRATCH_SIZE = 0x40000000   # 1GB

from dump_blob_paint_golden import WORK_SIZE

A_1 = 0x58EBC0      # RebuildAndSplitRegions
A_2 = 0x58EF10      # RebuildRegions（含开通路）
A_3 = 0x5A19E0      # TrimCliffCorners
A_4 = 0x578E60      # RepaintAll(0, -1)
A_5 = 0x5A17F0      # RerollMultitileCliffs

G_TILETBL = 0xA8ED2C
G_TILECNT = 0xA8ED38
G_CLIFF = 0xAA1020
G_SSP = 0xABC1F8
G_PAVED = 0xABBEC8
G_PAVEDEND = 0xABBEC4

CLIFF = 39          # temperate CliffSet 起始砖号（tileset_starts.txt 实锤）
SSP = 200           # SlopeSetPieces 合成值（10 块）
PAVED = 300         # PavedRoads 合成值（21 块；0x4866D0 只认前 0x0F 块）
PAVEDEND = 340      # PavedRoadEnds 合成值（4 块）


class MakingRegionsOracle(RegionOracle):
    def __init__(self, w, h, seed, players, water, map_type, usable,
                 region_size, accessibility, profile="mix"):
        RegionOracle.__init__(self, w, h, seed, players, water, map_type,
                              usable, profile)
        # 阶段 4 专属两个标量（MapSeedClass 静态实例的字段）
        self.wr32(SEEDOBJ + 0x70, region_size)       # +0x70 = 0xABE048
        self.wr32(SEEDOBJ + 0x6C, accessibility)     # +0x6C = 0xABE044
        # 0x89A304 防重表：8 方向在 **work 索引** 上的增量 = dy*side + dx。
        # 实机由 MapClass::Set_Dimensions（0x5671A5 调 0x42AC00）按 side 写入
        # {0x89A304..0x89A320}={-side,1-side,1,1+side,side,side-1,-1,-1-side}，
        # 与 0x89F688 的 (dx,dy) 逐项对应（2026-09-28 实机进程取证 + 反汇编双重
        # 实锤）。SplitRegion 扩散里 0x58DA5B `work[弹格索引+tab[8k+2]]·queued=-3`
        # 标记**候选格本身**（d 走 0/2/4/6 ⇒ N/E/S/W 四向），0x58D9EA 检查候选格
        # +0x3C==-3 命中即跳过 → **防重有效，命中不耗 RNG**。
        # 本基准必须与 _emu_regions.py 同口径（它已灌全量 deltas）——golden 是
        # CLI 期望值的唯一来源，CLI 走防重版（实机对拍 62298 四帧 0 diff）。
        kdx = [0, 1, 1, 1, 0, -1, -1, -1]
        kdy = [-1, -1, 0, 1, 1, 1, 0, -1]
        deltas = [kdy[k] * self.side + kdx[k] for k in range(8)]
        self.uc.mem_write(0x89A304, b"".join(
            struct.pack("<i", v) for v in deltas))
        # 砖表实体化
        self._build_tiles()
        # 贴砖器暂存全局清一遍（0x578E60 收尾自己也会清）
        self.wr32(0x880990, 0)
        self.wr32(0x88098C, 0)

    # ---- 带对齐桶的回收分配器 ----
    # RegionOracle 的版本按**请求字节数精确匹配**——阶段 3 的分配模式命中率高。
    # 阶段 4 的 0x58D620 分裂循环里，邻区标记表按当前区域 id 数分配
    # （`operator_new(DAT_00abed14)`），每次分裂 id +1，请求大小 +2 一档地涨，
    # 精确匹配永不命中 → 碎片累积把 64MB scratch 吃光。这里改成
    # 16 字节对齐桶：15242/15244/15246/15248 都进 15248 桶，复用率大增。
    # 对齐桶比请求大，bump 分配器本来就只看下限，安全。
    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        b = (n + 15) & ~15
        free = self.pool.get(b)
        if free:
            p = free.pop()
            uc.mem_write(p, b"\0" * n)
        else:
            p = self._bump(b)
        self.owned[p] = b
        uc.reg_write(UC_X86_REG_EAX, p)
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        p = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        b = self.owned.pop(p, None)
        if b:
            self.pool.setdefault(b, []).append(p)
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    # ---- 伪造 [0xA8ED2C] 砖表 ----
    def _build_tiles(self):
        self.ntiles = 900
        tbl = self._bump(self.ntiles * 4)
        for i in range(self.ntiles):
            self.wr32(tbl + i * 4, 0)
        self.tiletbl = tbl
        self.wr32(G_TILETBL, tbl)
        self.wr32(G_TILECNT, self.ntiles)

        # CliffSet 40 块：2x2 平砖（全 present、高 0、坡 0）。
        # ⚠️ 必须有多格砖：0x5A17F0 的判据 `邻格子块号 < 本格子块号`
        # 要 w*h > 1 才可能有非零子块号，全 1x1 的话整段空转。
        for k in range(40):
            self.make_tile(CLIFF + k, 2, 2,
                           [(1, 0)] * 4 + [])       # make_tile 按 y*2+x 排
            self.wr32(tbl + (CLIFF + k) * 4, self.tt)
        # SlopeSetPieces 10 块：1x1 平砖（BuildRamp 肩膀砖，StampTile 直压）
        for k in range(10):
            self.make_tile(SSP + k, 1, 1, [(1, 0)])
            self.wr32(tbl + (SSP + k) * 4, self.tt)
        # PavedRoads 21 块：1x1（BuildBridge 桥头路砖）
        for k in range(21):
            self.make_tile(PAVED + k, 1, 1, [(1, 0)])
            self.wr32(tbl + (PAVED + k) * 4, self.tt)
        # PavedRoadEnds 4 块：1x1
        for k in range(4):
            self.make_tile(PAVEDEND + k, 1, 1, [(1, 0)])
            self.wr32(tbl + (PAVEDEND + k) * 4, self.tt)

    # ---- 阶段 4 五入口 ----
    def run_stage4(self):
        self.rng_calls = 0
        r1 = self.rng_calls
        self._call(A_1, 0)
        r2 = self.rng_calls
        ret2 = self._call(A_2, 0)
        r3 = self.rng_calls
        self._call(A_3, 0)
        r4 = self.rng_calls
        self._call(A_4, 0, (0, 0xFFFFFFFF))
        r5 = self.rng_calls
        self._call(A_5, 0)
        r6 = self.rng_calls
        return (r2 - r1, r3 - r2, r4 - r3, r5 - r4, r6 - r5), ret2 & 0xFF

    # ---- 观测 ----
    def cell_state(self, x, y):
        c = self.cell(x, y)
        r = self.work + (y * self.side + x) * WORK_SIZE
        return (self.rd32(c + 0x38),
                self.uc.mem_read(c + 0x11A, 1)[0],
                struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                self.uc.mem_read(c + 0x11C, 1)[0],
                self.rd32(c + 0x44),
                self.uc.mem_read(c + 0x11E, 1)[0],
                sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)))

    def snapshot_all(self):
        out = {}
        for (x, y) in self._diamond():
            out[(x, y)] = self.cell_state(x, y)
        return out

    def regions(self):
        out = []
        data = self.rd32(REGVEC + 0x04)
        for i in range(self.rd32(REGVEC + 0x10)):
            r = self.rd32(data + i * 4)
            out.append((sx(self.rd32(r + 0x08)), sx(self.rd32(r + 0x0C)),
                        sx(self.rd32(r + 0x10)),
                        self.uc.mem_read(r + 0x14, 1)[0],
                        self.rd32(r + 0x16) & 0xFFFF,
                        (self.rd32(r + 0x16) >> 16) & 0xFFFF,
                        self.uc.mem_read(r + 0x1A, 1)[0]))
        return out

    # ---- 合成地形（0x58EF10 重建时干区域才走 0x58CAF9 取数）----
    def synth_terrain(self, kind):
        u"""在阶段 3 之上直接摆地形。kind 见下。

        ⚠️ 层高一律取 4 的倍数（0/4/8）：0x58D620 切分后给新区域定层高时，
        候选列表只在邻区层高差 ∈ {0,4,8} 时才有内容（反编译三分支），
        差 1/2/3/5/6/7 会拿到空候选列表 → PickBelow(0) 越界读（真 UB）。
        真实地图层高粒度就是 4：FillNotch +4 / TrimCliffCorners ±4 /
        压砖器 -4 全是 ±4，从 0 起步所以全域都是 4 的倍数。"""
        for (x, y) in self._diamond():
            if kind == "dry-bands":
                # 全图未定砖（0xFFFF）：CreateRegions 建不出区域，
                # 0x58EF10 重建时每格都 SeedRegion（干 → 0x58CAF9 取数）。
                # ⚠️ 必须用 UNDECIDED 而不是 tile=1：0x58D620 扩散剥走
                # 要求 IsPlain（0x486380，tile∈{0,0xFFFF}），tile=1 剥不动
                # → Split 剥 0 格也返回成功 → 0x58EBC0 无限 restart。
                # 层高 0/4 交替的斜带：邻区差恒 4，切分走合法分支
                tile, lvl = UNDECIDED, 4 * (((x + y) // 6) % 2)
            elif kind == "dry-noise":
                # 逐格跳但只取 0/4/8：邻区差 ∈ {4,8}，切分候选非空
                tile, lvl = UNDECIDED, 4 * (((x * 7 + y * 13) // 5) % 3)
            elif kind == "water-lake":
                inside = abs(x - (self.w + self.h // 2)) + \
                    abs(y - (self.h + self.w // 2)) < 6
                tile = WATER if inside else UNDECIDED
                lvl = 0
            elif kind == "green-bands":
                tile = GREEN if (x + y) % 3 else UNDECIDED
                lvl = 4 * (((x + y) // 8) % 2)
            elif kind == "terrace":
                # 中央一块 +4 台地，四周未定砖平底：
                # AutoCliff（第二遍只动未定砖）会沿台地边贴悬崖砖，
                # 0x5A17F0 才有内容可重掷
                d = abs(x - (self.w + self.h // 2)) + \
                    abs(y - (self.h + self.w // 2))
                tile = UNDECIDED
                lvl = 4 if d < 4 else 0
            elif kind == "terrace-water":
                # 台地 + 两片分开的水：wet 区域和干区域都有，
                # MakeConnections 的陆地对坡道 + 水域对架桥都能触发
                d = abs(x - (self.w + self.h // 2)) + \
                    abs(y - (self.h + self.w // 2))
                in_water = (abs(x - (self.w + 2)) + abs(y - self.h) < 3) or \
                    (abs(x - (self.w + self.h - 2)) + abs(y - self.h) < 3)
                tile = WATER if in_water else UNDECIDED
                lvl = 4 if d < 4 else 0
            elif kind == "rs99":
                # 全干 + 中央湖 + 中央台地，rs=99：maxCells=183，干区 309 ≥ 303
                # → 0x58D620 真分裂收敛（剥走块 ≥101 独立成区）。
                # ⚠️ 干区 <303 会 Split 死循环，此方案只在 14×12（干区 309）
                # 验证过收敛；笛卡尔中心（与探针一致）。
                cx, cy = self.w // 2, self.h // 2
                lake = abs(x - cx) < 3 and abs(y - cy) < 3
                terr = abs(x - cx) < 4 and abs(y - cy) < 4 and not lake
                if lake:
                    tile, lvl = WATER, 0
                else:
                    tile, lvl = UNDECIDED, 4 if terr else 0
            elif kind == "checker":
                # 棋盘：UNDECIDED 4×4 干块 + GREEN 2 格湿带。
                # 干块 16 格 ≤ maxCells(rs=0→16) → 0 次 Split 收敛。
                # 干块层高 0/4 交替（邻区差 4，Split 候选合法）。
                bx, by = x // 6, y // 6
                ix, iy = x % 6, y % 6
                if ix < 4 and iy < 4:
                    tile, lvl = UNDECIDED, 4 * ((bx + by) % 2)
                else:
                    tile, lvl = GREEN, 0
            elif kind == "checker-t":
                # 棋盘 + 中央 6×6 台地（层高 4）：SeedRegion 取数有料。
                cx, cy = self.w // 2, self.h // 2
                bx, by = x // 6, y // 6
                ix, iy = x % 6, y % 6
                terr = (abs(x - cx) < 3 and abs(y - cy) < 3)
                if ix < 4 and iy < 4:
                    tile, lvl = UNDECIDED, \
                        4 if terr else 4 * ((bx + by) % 2)
                else:
                    tile, lvl = GREEN, 0
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)


REGVEC = 0xABDF90
WATER = 314
UNDECIDED = 0xFFFF
GREEN = 493


def sx(v):
    return v - (1 << 32) if v >> 31 else v


# 名字, W, H, 种子, 玩家, WaterAmount, MapType, 可用区, RegionSize,
# Accessibility, 剖面, 地形（stage2 = 真身跑；其余 = synth_terrain）
CASES = [
    # ── MapType 3/4 真实地形（stage2 铺水后陆地少、水多）──
    ("mt3-p2", 12, 10, 0x12345678, 2, 60, 3, (0, 0, 12, 10), 0, 90,
     "mix", "stage2"),
    ("mt3-p4-rs8", 14, 12, 0x5A5A5A5A, 4, 100, 3, (0, 0, 14, 12), 8, 50,
     "mix", "stage2"),
    ("mt4-p2", 12, 10, 0x0BADF00D, 2, 0, 4, (0, 0, 12, 10), 0, 90,
     "mix", "stage2"),
    ("mt3-p4-big", 24, 20, 0x1234ABCD, 4, 80, 3, (0, 0, 24, 20), 0, 90,
     "mix", "stage2"),
    # ── 合成地形（全部经验证收敛；⚠️ 大连续干区 n<303 会触发
    #    0x58D620 Split 死循环，任何新方案必须先跑 _probe_mr3al 式预检）──
    # rs=99：maxCells=183，干区 309 ≥ 303 → 真分裂收敛（0x58EBC0 深度取数）
    ("syn-rs99", 14, 12, 0x2468ACE0, 2, 0, 3, (0, 0, 14, 12), 99, 90,
     "mix", "rs99"),
    ("syn-rs99-acc0", 14, 12, 0x13579BDF, 2, 0, 3, (0, 0, 14, 12), 99, 0,
     "mix", "rs99"),
    # 棋盘：干块 4×4=16 ≤ maxCells(rs=0→16) → 0 次 Split
    ("syn-checker", 14, 12, 0x2468ACE0, 2, 0, 3, (0, 0, 14, 12), 0, 90,
     "mix", "checker"),
    ("syn-checker-acc0", 14, 12, 0x0F0F0F0F, 2, 0, 3, (0, 0, 14, 12), 0, 0,
     "mix", "checker"),
    # 棋盘 + 中央台地（0x58EF10 SeedRegion 取数 103）
    ("syn-checker-t", 14, 12, 0x0BADF00D, 2, 0, 3, (0, 0, 14, 12), 0, 90,
     "mix", "checker-t"),
    ("syn-checker-t-acc101", 14, 12, 0x1234ABCD, 2, 0, 3, (0, 0, 14, 12), 0,
     101, "mix", "checker-t"),
]

HEAD = (u"# 阶段 4 Making regions 五入口整链模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_making_regions_golden.py\n"
        u"# 输入 = 阶段 3 之后的完整状态（stage2 真身 或 synth_terrain 合成）\n"
        u"# IN  x y tile subtile level ramp overlay overlayData owner queued\n"
        u"# INREG id level wet seedX seedY（阶段 3 之后区域表，测试重建输入）\n"
        u"# OUT 同 IN（阶段 4 五入口之后的终态）\n"
        u"# REG id cellCount level wet seedX seedY finalized\n"
        u"# CliffSet=39（真实）SlopeSetPieces=200 PavedRoads=300"
        u" PavedRoadEnds=340（合成，与 C++ 测试一致）\n")


def main():
    out = []
    for (name, w, h, seed, players, water, mt, usable, rs, acc,
         profile, terrain) in CASES:
        o = MakingRegionsOracle(w, h, seed, players, water, mt, usable, rs,
                                acc, profile)
        if terrain == "stage2":
            o.stage2()
        else:
            o.synth_terrain(terrain)
        o.init_regions()
        inregs = o.regions()               # 阶段 3 之后的区域表（C++ 测试重建输入）
        nextid_in = o.rd32(0xABED14)
        before = o.snapshot_all()
        (c1, c2, c3, c4, c5), ret2 = o.run_stage4()
        after = o.snapshot_all()
        regs = o.regions()
        dummy = o.rd32(0xABDC50 + 0x24)
        head = ("CASE %s %d %d seed=0x%08X players=%d water=%d mt=%d"
                " usable=%d,%d,%d,%d rs=%d acc=%d tiles=%s terrain=%s"
                " -> rng=[%d,%d,%d,%d,%d] ret2=%d regions=%d nextid=%d"
                " nextid_in=%d dummy=%d,%d"
                % (name, w, h, seed, players, water, mt, usable[0], usable[1],
                   usable[2], usable[3], rs, acc, profile, terrain, c1, c2, c3,
                   c4, c5, ret2, len(regs), sx(o.rd32(0xABED14)),
                   sx(nextid_in), dummy & 0xFFFF, (dummy >> 16) & 0xFFFF))
        out.append(head)
        for (x, y) in sorted(before):
            t, st, lv, rp, ov, od, ow, q = before[(x, y)]
            out.append("  IN %d %d %d %d %d %d %d %d %d %d"
                       % (x, y, t, st, lv, rp, ov, od, ow, q))
        for (rid, cnt, lv, wet, sxx, syy, fin) in inregs:
            out.append("  INREG %d %d %d %d %d" % (rid, lv, wet, sxx, syy))
        for (x, y) in sorted(after):
            t, st, lv, rp, ov, od, ow, q = after[(x, y)]
            out.append("  OUT %d %d %d %d %d %d %d %d %d %d"
                       % (x, y, t, st, lv, rp, ov, od, ow, q))
        for (rid, cnt, lv, wet, sxx, syy, fin) in regs:
            out.append("  REG %d %d %d %d %d %d %d"
                       % (rid, cnt, lv, wet, sxx, syy, fin))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/making_regions_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
