# -*- coding: utf-8 -*-
"""三种 MapType 投放策略 0x59AD10 / 0x59AFA0 / 0x59B200 的整函数模拟执行。

这是比涂块核心更外一层的对拍：涂块只负责"从一个种子长出一坨陆地"，
这三个函数决定"在哪里、投多少、投几次"——整张地图的陆地骨架由它们定。
基准记录的是**全过程被涂成陆地的格子顺序**（跨所有涂块调用），
外加随机数消耗量与收尾时的块编号。

运行环境沿用 dump_blob_paint_golden.PaintOracle，另加：
  [0x87F8DC]/[0x87F8E0]  地图 W/H（0x42B1F0 的覆盖率分母要用）
  [0x87F8E4..F0]         可用区域矩形
  [0xABE2F0..FC]         全图边界；这里给 (-1,-1,-1,-1)，
                         让 InRegion / DirectionalCost 都走真实分支而不是快捷路径
  工作记录 +0x00         打包坐标（阶段 1 的产物，MapType 2 的线性扫描要读）
  MapSeedClass           +0x4C WaterAmount / +0x50 NumPlayers / +0x308 块编号 / +0x30C=4

桩与涂块基准一致：IsPaintable 恒真、岸线平滑 0x57A0C0 恒真。
"""
import sys

from dump_blob_paint_golden import PaintOracle, WORK_SIZE

STRATEGY = {0: 0x59AD10, 1: 0x59AFA0, 2: 0x59B200}


class PlaceOracle(PaintOracle):
    def __init__(self, w, h, seed, players, water, usable):
        PaintOracle.__init__(self, w, h, seed)
        self.wr32(0x87F8DC, w)
        self.wr32(0x87F8E0, h)
        for i, v in enumerate(usable):
            self.wr32(0x87F8E4 + i * 4, v)
        for i in range(4):
            self.wr32(0xABE2F0 + i * 4, 0xFFFFFFFF)
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE, (y << 16) | x)
        self.seedobj = self._bump(0x400)
        self.wr32(self.seedobj + 0x4C, water)
        self.wr32(self.seedobj + 0x50, players)
        self.wr32(self.seedobj + 0x308, 0)
        self.wr32(self.seedobj + 0x30C, 4)

    def run(self, map_type):
        self.painted = []
        self.rng_calls = 0
        self._call(STRATEGY[map_type], self.seedobj)
        return list(self.painted), self.rng_calls, self.rd32(self.seedobj + 0x308)


# (w, h, seed, map_type, players, water, usable)
CASES = [
    (30, 30, 0x12345678, 0, 2, 0, (0, 0, 30, 30)),
    (30, 30, 0x0BADF00D, 0, 8, 50, (1, 1, 28, 28)),
    (24, 24, 0xDEADBEEF, 0, 4, 100, (0, 0, 24, 24)),
    (30, 30, 0x12345678, 1, 2, 0, (0, 0, 30, 30)),
    (30, 30, 0x0BADF00D, 1, 4, 50, (1, 1, 28, 28)),
    (24, 24, 0x5A5A5A5A, 1, 2, 100, (0, 0, 24, 24)),
    (30, 30, 0x12345678, 2, 2, 0, (0, 0, 30, 30)),
    (30, 30, 0x00C0FFEE, 2, 4, 50, (1, 1, 28, 28)),
    (24, 24, 0x0F0F0F0F, 2, 2, 100, (0, 0, 24, 24)),
    (40, 24, 0x1A2B3C4D, 0, 6, 20, (0, 0, 40, 24)),
    (40, 24, 0x77777777, 1, 6, 20, (0, 0, 40, 24)),
    (40, 24, 0xABCDEF01, 2, 6, 20, (0, 0, 40, 24)),
]


def main():
    out = []
    for (w, h, seed, mt, players, water, usable) in CASES:
        o = PlaceOracle(w, h, seed, players, water, usable)
        painted, calls, stamp = o.run(mt)
        out.append("CASE %d %d 0x%08X mt=%d players=%d water=%d usable=%d,%d,%d,%d"
                   " -> cells=%d rng=%d stamp=%d" %
                   (w, h, seed, mt, players, water, usable[0], usable[1],
                    usable[2], usable[3], len(painted), calls, stamp))
        out.append("  ORDER " + " ".join("%d:%d" % p for p in painted))
        print(out[-2])
        sys.stdout.flush()
    head = (u"# \u4e09\u79cd MapType \u6295\u653e\u7b56\u7565\u7684\u6574\u51fd"
            u"\u6570\u6a21\u62df\u6267\u884c\u57fa\u51c6\n"
            u"# \u751f\u6210\u811a\u672c code/randmap/dump_place_blobs_golden.py\n"
            u"# ORDER \u884c\u662f\u8de8\u6240\u6709\u6d82\u5757\u8c03\u7528\u7684"
            u"\u9646\u5730\u751f\u957f\u987a\u5e8f X:Y\n")
    with open("../../memory/data/rmg/place_blobs_golden.txt", "w",
              encoding="utf-8") as f:
        f.write(head + "\n".join(out) + "\n")


if __name__ == "__main__":
    main()
