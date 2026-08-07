# -*- coding: utf-8 -*-
u"""水面细化 0x59C630 的整函数模拟执行。

初态不是手搓的，而是**先把阶段 2 调度器 0x59A6C0 整个跑一遍**——
那样水面的分布、子格号、工作数组归属全是原版自己算出来的，
细化器面对的输入才有代表性。随后再调 0x59C630 真身。

比 stage_water 那份基准多两样：
  1) 全局砖表 [0xA8ED2C] 要把 **WaterSet 那 0x0E 块**也实体化——
     0x59C795 会以 TileTypes[WaterSet + shape] 取砖再解虚表，没有空指针保护。
     尺寸按原版实情合成：前 8 块 2x2（大水面），后 6 块 1x1（小浪花）。
  2) 每格多记一个 CellClass **+0x11C**（坡度）。压砖器 0x5A6C10 无条件写它，
     而贴砖器 0x57B440 从来不写——这是两者可观测的差别之一。
"""
import sys

from dump_stage_water_golden import StageOracle, GREEN
from dump_shore_smooth_golden import WATER, SHORE
from dump_blob_paint_golden import WORK_SIZE

A_REFINE = 0x59C630

WATER_TILES = 0x0E      # WaterSet 的 TilesInSet（theater ini 实测）


def water_tile_size(k):
    u"""第 k 块水砖的合成尺寸：前 8 块 2x2，后 6 块 1x1。

    这条规则同时也是 0x59C630 两条分支的分工——大水面压 shape ∈ [0,7]，
    小浪花只换自己一格的砖号（WaterSet + 8 .. +13）。
    """
    return (2, 2) if k < 8 else (1, 1)


def water_tile_ramp(k, j):
    u"""子块 +0x2A 的合成值。取不同的非零值，好让 +0x11C 的写入可观测。"""
    return 1 + (k * 5 + j * 3) % 17


class RefineOracle(StageOracle):
    def __init__(self, w, h, seed, players, water, map_type, usable,
                 profile="mix"):
        StageOracle.__init__(self, w, h, seed, players, water, map_type,
                             usable, profile)
        old = self.rd32(0xA8ED2C)
        n = WATER + WATER_TILES + 4
        table = self._bump(4 * n)
        for k in range(0x2A):
            self.wr32(table + (SHORE + k) * 4, self.rd32(old + (SHORE + k) * 4))
        for k in range(WATER_TILES):
            tw, th = water_tile_size(k)
            blocks = [(True, 0, water_tile_ramp(k, j)) for j in range(tw * th)]
            obj = self.make_tile(WATER + k, tw, th, blocks)
            self.wr32(table + (WATER + k) * 4, obj)
        self.wr32(0xA8ED2C, table)
        self.wr32(0x880990, 0)          # make_tile 顺手写过它，复位

    def snapshot5(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            if not c:
                continue
            out[(x, y)] = (
                self.rd32(c + 0x38),
                self.uc.mem_read(c + 0x11A, 1)[0],
                self.uc.mem_read(c + 0x11B, 1)[0],
                self.uc.mem_read(c + 0x11C, 1)[0],
                self.rd32(self.work + (y * self.side + x) * WORK_SIZE + 0x38))
        return out

    def run(self):
        self.rng_calls = 0
        self._call(0x59A6C0, self.seedobj)
        stage_calls = self.rng_calls
        self._call(A_REFINE, 0)
        return (stage_calls, self.rng_calls, self.rd32(0xABDC50 + 0x38),
                self.snapshot5())


# 与 stage_water 基准同一批用例（同种子同参数），产物直接可比
CASES = [
    ("mt0-p2", 20, 20, 0x12345678, 2, 0, 0, (0, 0, 20, 20), "mix"),
    ("mt0-p4", 20, 18, 0x0BADF00D, 4, 50, 0, (1, 1, 18, 16), "mix"),
    ("mt0-allwater", 10, 10, 0x12345678, 2, 0, 0, (0, 0, 10, 10), "mix"),
    ("mt1-p2", 10, 10, 0x12345678, 2, 0, 1, (0, 0, 10, 10), "mix"),
    ("mt1-p4", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "mix"),
    ("mt2-p2", 10, 10, 0x00C0FFEE, 2, 0, 2, (0, 0, 10, 10), "mix"),
    ("mt2-p4", 12, 10, 0x0F0F0F0F, 4, 20, 2, (0, 0, 12, 10), "mix"),
    ("mt0-unit", 20, 20, 0x1A2B3C4D, 2, 0, 0, (0, 0, 20, 20), "unit"),
    ("mt1-unit", 10, 10, 0x77777777, 2, 30, 1, (0, 0, 10, 10), "unit"),
    ("mt2-unit", 10, 10, 0xABCDEF01, 2, 30, 2, (0, 0, 10, 10), "unit"),
    ("mt3-nostrategy", 10, 10, 0x1234, 2, 0, 3, (0, 0, 10, 10), "mix"),
    ("mt0-big", 30, 30, 0xDEADBEEF, 6, 40, 0, (0, 0, 30, 30), "mix"),
]


HEAD = (u"# \u6c34\u9762\u7ec6\u5316 0x59C630 \u6574\u51fd\u6570\u6a21\u62df"
        u"\u6267\u884c\u57fa\u51c6\uff08\u5148\u8dd1\u5b8c 0x59A6C0\uff09\n"
        u"# \u751f\u6210\u811a\u672c code/randmap/dump_water_refine_golden.py\n"
        u"# CELL \u884c = \u7ec8\u6001 x y tile subtile level ramp workBlob\n"
        u"# WaterSet=314\uff080..7 \u4e3a 2x2\uff0c8..13 \u4e3a 1x1\uff09"
        u" ShorePieces=89 GreenTile=493\n")


def main():
    out = []
    for (name, w, h, seed, players, water, mt, usable, profile) in CASES:
        o = RefineOracle(w, h, seed, players, water, mt, usable, profile)
        stage_calls, total_calls, dummy, after = o.run()
        head = ("CASE %s %d %d seed=0x%08X players=%d water=%d mt=%d"
                " usable=%d,%d,%d,%d tiles=%s -> rngstage=%d rngtotal=%d"
                " dummy=%d"
                % (name, w, h, seed, players, water, mt, usable[0], usable[1],
                   usable[2], usable[3], profile, stage_calls, total_calls,
                   dummy))
        out.append(head)
        for (x, y) in sorted(after):
            t, st, lv, rp, wb = after[(x, y)]
            if wb >> 31:
                wb -= 1 << 32
            out.append("  CELL %d %d %d %d %d %d %d" % (x, y, t, st, lv, rp, wb))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/water_refine_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
