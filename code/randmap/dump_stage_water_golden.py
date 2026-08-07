# -*- coding: utf-8 -*-
u"""阶段 2 调度器 0x59A6C0 的整函数模拟执行。

这是本模块目前最"整"的一次对拍：一次调用把铺水 → 投放策略（0x59AD10 /
0x59AFA0 / 0x59B200）→ 涂块核心（0x59BBC0）→ 岸线平滑四遍（0x57A0C0）→
掩码（0x57B210）→ 贴砖器（0x57B440）全都真身跑一遍，只留 IsPaintable
一个桩。产出就是整张地图的终态。

环境在 SmoothOracle（已备好真身岸线所需的砖表与落点偏移表）之上再加：
  [0x87F8E4..F0]   可用区域矩形（投放策略的分区依据）
  [0xABE2F0..FC]   全图边界，给 (-1,-1,-1,-1) 让 InRegion 走真实分支
  工作记录 +0x00   打包坐标（MapType 2 的线性扫描要读）
  [0xAA0E18]       GreenTile（收尾刷绿的砖号，temperate = 493）
  [0xABDF94/A0]    RegionClass 数组置空 —— 0x59A7C2 那段拆除循环直接跳过
  MapSeedClass     +0x3C MapType / +0x4C WaterAmount / +0x50 NumPlayers
                   / +0x308 块编号 / +0x30C 回滚层高

基准记：随机数调用次数、收尾块编号、每格终态 (tile, subtile, level, workBlob)，
外加**共享哑元 0xABDC50 的 +0x38** —— 收尾刷绿会写它，而它一旦不再"未定砖"
就会让后续所有出图邻格被跳过，是能改变产物的可观测状态。
"""
import sys

from dump_shore_smooth_golden import SmoothOracle, WATER, SHORE
from dump_blob_paint_golden import WORK_SIZE

A_STAGE = 0x59A6C0
GREEN = 493        # temperate GreenTile（tileset_starts.txt）


class StageOracle(SmoothOracle):
    def __init__(self, w, h, seed, players, water, map_type, usable,
                 profile="mix"):
        SmoothOracle.__init__(self, w, h, seed, profile)
        for i, v in enumerate(usable):
            self.wr32(0x87F8E4 + i * 4, v)
        for i in range(4):
            self.wr32(0xABE2F0 + i * 4, 0xFFFFFFFF)
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE, (y << 16) | x)
        self.wr32(0xAA0E18, GREEN)
        self.wr32(0xABDF94, 0)
        self.wr32(0xABDFA0, 0)
        self.wr32(0xABED14, 0)
        self.seedobj = self._bump(0x400)
        self.wr32(self.seedobj + 0x3C, map_type)
        self.wr32(self.seedobj + 0x4C, water)
        self.wr32(self.seedobj + 0x50, players)
        self.wr32(self.seedobj + 0x308, 0)
        self.wr32(self.seedobj + 0x30C, 4)

    def run(self):
        self.rng_calls = 0
        self._call(A_STAGE, self.seedobj)
        return (self.rng_calls, self.rd32(self.seedobj + 0x308),
                self.rd32(0xABDC50 + 0x38), self.snapshot(),
                self.rd32(self.work + 0x38), self.rd32(self.work + 0x3C))


# 名字, W, H, 种子, 玩家数, WaterAmount, MapType, 可用区, 砖尺寸剖面
CASES = [
    # MapType 0 的区域是内切椭圆，图太小时每块都铺不出岸线、整块回滚
    # （终态全是水）。那也是原版真实行为，留一例；其余用够大的图取有陆地的样本。
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
    # MapType 3：0x59A702 的 jne 直接落到收尾——铺完水就没人涂陆地了
    ("mt3-nostrategy", 10, 10, 0x1234, 2, 0, 3, (0, 0, 10, 10), "mix"),
    ("mt0-big", 30, 30, 0xDEADBEEF, 6, 40, 0, (0, 0, 30, 30), "mix"),
    # 真实 .tmp 尺寸（岸砖 7 种、水砖 8 块 2x2 + 6 块 1x1）
    ("mt0-real", 20, 20, 0x12345678, 2, 0, 0, (0, 0, 20, 20), "real"),
    ("mt1-real", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "real"),
    ("mt2-real", 12, 10, 0x0F0F0F0F, 4, 20, 2, (0, 0, 12, 10), "real"),
]


HEAD = (u"# \u9636\u6bb5 2 \u8c03\u5ea6\u5668 0x59A6C0 \u6574\u51fd\u6570"
        u"\u6a21\u62df\u6267\u884c\u57fa\u51c6\n"
        u"# \u751f\u6210\u811a\u672c code/randmap/dump_stage_water_golden.py\n"
        u"# CELL \u884c = \u7ec8\u6001 x y tile subtile level workBlob\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493\uff08temperate\uff09\n")


def main():
    out = []
    for (name, w, h, seed, players, water, mt, usable, profile) in CASES:
        o = StageOracle(w, h, seed, players, water, mt, usable, profile)
        calls, stamp, dummy, after, w38, w3c = o.run()
        head = ("CASE %s %d %d seed=0x%08X players=%d water=%d mt=%d"
                " usable=%d,%d,%d,%d tiles=%s -> rng=%d stamp=%d dummy=%d"
                " work38=%d work3c=%d"
                % (name, w, h, seed, players, water, mt, usable[0], usable[1],
                   usable[2], usable[3], profile, calls, stamp, dummy,
                   w38 - (1 << 32) if w38 >> 31 else w38,
                   w3c - (1 << 32) if w3c >> 31 else w3c))
        out.append(head)
        for (x, y) in sorted(after):
            t, st, lv, wb = after[(x, y)]
            if wb >> 31:
                wb -= 1 << 32
            out.append("  CELL %d %d %d %d %d %d" % (x, y, t, st, lv, wb))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/stage_water_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
