# -*- coding: utf-8 -*-
u"""阶段 9 第 ① 步「打锚点」0x5A33F0 的整函数模拟执行。

它一个随机数都不取，只按扫图顺序读每个格子的 (砖号, 子格号)，
再往邻格的工作记录里写 +0x08 = 0.5（只有岸砖那支）与 +0x45 = 1。
环境沿用贴砖器那套 PlaceOracle：格子指针数组、工作数组、side、菱形边界、
以及 0x4863D0 要用的一整排 theater 砖号全局都已就位。

基准记「工作数组里所有被改动过的槽位」：下标、height 的 8 字节原始位型、
以及 +0x45。height 收位型而不是十进制，是因为这一遍只会写常量 0.5，
位型能顺手证明「立即数 0x3FE0000000000000」这条。
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle, THEATER
from dump_blob_paint_golden import WORK_SIZE

A_ANCHOR = 0x5A33F0

WATER = THEATER[0xAA0738]      # 314 WaterSet
SHORE = THEATER[0xABAD28]      # 89  ShorePieces
CLIFF = THEATER[0xAA1020]      # 39  CliffSet     (+0x28 宽)
RAMPS = THEATER[0xABBEBC]      # 601 CliffRamps   (+0x14 宽，注意不是整组)
BRIDGE = THEATER[0xAA0E28]     # 277 BridgeSet    (+0x10 宽)
WFEAST = THEATER[0xAA073C]     # 539 WaterfallEast（这一支还要看子格号）
WCLIFF = THEATER[0xAA101C]     # 148 WaterCliffs  (+0x1C 宽)
DESTR = THEATER[0xABC2C8]      # 761 DestroyableCliffs (+2 宽)
UNDECIDED = 0
UNDECIDED2 = 0xFFFF


class AnchorOracle(PlaceOracle):
    def __init__(self, w, h, seed=1):
        PlaceOracle.__init__(self, w, h, seed)
        # 工作记录 +0x00 打包坐标：阶段 1 只给菱形内的格子写（stage-init.md §五）。
        # 这一遍不读它，但下一步 0x5A2F50 要，摆上免得基准之间不一致。
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE,
                      (y << 16) | x)

    def seed_map(self, tile_of):
        for (x, y) in self._diamond():
            t, st = tile_of(x, y)
            self.set_cell(x, y, tile=t, subtile=st, level=0)

    def snapshot_work(self):
        out = {}
        n = self.side * self.side
        for i in range(n):
            r = self.work + i * WORK_SIZE
            hi = bytes(self.uc.mem_read(r + 0x08, 8))
            lk = self.uc.mem_read(r + 0x45, 1)[0]
            out[i] = (hi, lk)
        return out

    def run(self):
        before = self.snapshot_work()
        self.rng_calls = 0
        self._call(A_ANCHOR, 0)
        after = self.snapshot_work()
        diff = [(i, after[i][0], after[i][1]) for i in sorted(after)
                if after[i] != before[i]]
        return diff, self.rng_calls


# ---- 地形生成器（C++ 侧逐字复刻同一套规则）----
def lcg_tiles(seed, palette):
    s = [seed & 0xFFFFFFFF]

    def f(x, y):
        s[0] = (s[0] * 1103515245 + 12345) & 0xFFFFFFFF
        v = (s[0] >> 16) & 0x7FFF
        spec = palette[v % len(palette)]
        base, span, sub = spec
        return (base + (v // len(palette)) % span if span else base), sub
    return f


def make_tiles(kind):
    if kind == "shore-only":
        # 全水，只有一条对角线是岸砖：邻格全是水（非未定砖）-> 一个锚点都打不出
        return lambda x, y: ((SHORE + (x % 0x2A), 0) if x == y else (WATER, 0))
    if kind == "shore-in-undecided":
        # 岸砖泡在未定砖里：八向的第一个（正北 dy=-1）必然命中
        return lambda x, y: ((SHORE + ((x + y) % 0x2A), 0)
                             if (x % 3 == 0 and y % 3 == 0) else (UNDECIDED, 0))
    if kind == "mixed":
        pal = [(WATER, 0, 0), (UNDECIDED, 0, 0), (UNDECIDED2, 0, 0),
               (SHORE, 0x2A, 0), (CLIFF, 0x28, 0), (RAMPS, 0x14, 0),
               (BRIDGE, 0x10, 0), (WCLIFF, 0x1C, 0), (DESTR, 2, 0)]
        return lcg_tiles(0xC0FFEE, pal)
    if kind == "waterfall-sub":
        # 0x4863D0 里唯一要看子格号的四支：砖号恰为 base 或 base+3 时，
        # 子格号决定判定翻不翻面。这里把两者都摆出来。
        pal = [(WFEAST, 0, 0), (WFEAST, 0, 4), (WFEAST + 3, 0, 0),
               (WFEAST + 3, 0, 2), (WFEAST + 1, 0, 0),
               (UNDECIDED, 0, 0), (UNDECIDED2, 0, 0), (WATER, 0, 0)]
        return lcg_tiles(0x5A5A, pal)
    if kind == "cliff-ring":
        # 悬崖围一圈未定砖：只打 lock、不给 0.5，与岸砖那支做对照
        return lambda x, y: ((CLIFF + ((x * 7 + y) % 0x28), 0)
                             if ((x + y) % 4 == 0) else (UNDECIDED, 0))
    if kind == "allundecided":
        return lambda x, y: (UNDECIDED, 0)
    if kind == "allwater":
        return lambda x, y: (WATER, 0)
    raise SystemExit("unknown kind " + kind)


CASES = [
    ("shore-only", 20, 20),
    ("shore-in-undecided", 20, 20),
    ("mixed", 20, 20),
    ("mixed", 26, 22),
    ("waterfall-sub", 20, 20),
    ("cliff-ring", 18, 16),
    ("allundecided", 14, 12),
    ("allwater", 14, 12),
]

HEAD = (u"# 阶段 9 锚点 0x5A33F0 整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_height_anchors_golden.py\n"
        u"# TILE 行 = 初态 x y tile subtile（只列菱形内格子）\n"
        u"# WORK 行 = 被改动的工作槽位 index heightBits(16 hex) lock\n")


def main():
    out = []
    for (kind, w, h) in CASES:
        o = AnchorOracle(w, h)
        f = make_tiles(kind)
        o.seed_map(f)
        # ⚠️ f 带内部状态（lcg），第二次调用给的值不同 —— 必须回放同一序列，
        # 所以上面 seed_map 与这里各建一次生成器
        o2 = make_tiles(kind)
        cells = [(x, y) + o2(x, y) for (x, y) in o._diamond()]
        diff, calls = o.run()
        head = ("CASE %s %d %d -> work=%d rng=%d" % (kind, w, h, len(diff), calls))
        out.append(head)
        for (x, y, t, st) in cells:
            out.append("  TILE %d %d %d %d" % (x, y, t & 0xFFFF, st))
        for (i, bits, lk) in diff:
            out.append("  WORK %d %s %d" % (i, bits[::-1].hex().upper(), lk))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/height_anchors_golden.txt")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(HEAD)
        fp.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
