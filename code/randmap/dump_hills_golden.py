# -*- coding: utf-8 -*-
u"""阶段 9「造山」整条链 0x5A35F0 的整函数模拟执行。

0x5A35F0 是自足的：它只往下调 0x5A33F0（锚点）、0x5A2F50（高度场）、
0x6B2A70（建角点格网）、0x6B4100 / 0x6B4240 / 0x6B3E60 / 0x6B3A80（抬升）、
0x6B3850（格网落回砖号）与格子迭代器。所以一次调用就能把 §一~§九 全打通。

⚠️ 除一条用例外，全部把 Ruggedness 设成 0：`baseSlope < 0.025` 时 0x5A2F50
在 0x5A2FA8 直接返回，**一个随机数都不抽**，于是整条基准里没有任何 x87
超越函数（fyl2x / fsqrt），不受 fast-math.md §4.1 那条「Unicorn 的 fyl2x
不可信」的影响。高度场改由脚本直接摆进工作记录 +0x08 —— 反正 0x5A2F50
本身已经有自己的基准（height_field_golden.txt）。

除了贴砖器那套环境（PlaceOracle），这里还要多摆四样：
  * 角点格网的矩形 MapClass +0x124..+0x130 = {1, 1, W+H-1, W+H-1}
    （Set_Dimensions 0x56631C 写的，RMG 传进去的是 {0,0,W,H}）
  * 撤销栈 DynamicVectorClass 0xB0B650：vtable / IsValid / GrowthStep=10。
    **不摆的话 Add 会静默失败**，回滚就什么也还原不了，地形完全不同。
  * 砖表 [0xA8ED2C] 与长度 [0xA8ED38]，每项 +0x2E0 = 可变形标志
  * 每个格子 +0x44 = -1（没有 overlay）；+0xE4 保持 0（没有 object）

基准收：跑完之后每个格子的 (砖号, 子格号, 层高, 坡面)、共享哑元的同四项、
以及 0x6B3850 入口处整张格网里 dirty 的角点。
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle, THEATER
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_HILLS = 0x5A35F0
A_APPLY = 0x6B3850

G_LAT = 0xB0B6EC
G_ROWS = 0x8759A0
G_STRIDE = 0x8759A4
G_ORGX = 0x8759A8
G_ORGY = 0x8759AC
G_TILETBL = 0xA8ED2C
G_TILECNT = 0xA8ED38
G_UNDO = 0xB0B650
MAPSEED = 0xABDFD8
DUMMY = 0xABDC50

CLEARTILE = 0          # 0xAA10B0 temperate
RAMPBASE = 29          # 0xABC1D8 temperate
WATER = THEATER[0xAA0738]      # 314
SHORE = THEATER[0xABAD28]      # 89
CLIFF = THEATER[0xAA1020]      # 39
UNDECIDED = 0xFFFF


class HillsOracle(PlaceOracle):
    def __init__(self, w, h, seed=1):
        PlaceOracle.__init__(self, w, h, seed)
        self.wr32(0xAA10B0, CLEARTILE)
        # 角点格网的矩形（MapClass +0x124..+0x130）
        self.wr32(0x87F90C, 1)
        self.wr32(0x87F910, 1)
        self.wr32(0x87F914, w + h - 1)
        self.wr32(0x87F918, w + h - 1)
        self.wr32(G_LAT, 0)
        # 撤销栈：vtable 0x7F327C（vtable[8] = 0x6B4650 Resize），GrowthStep 10
        self.wr32(G_UNDO + 0x00, 0x7F327C)
        self.wr32(G_UNDO + 0x04, 0)
        self.wr32(G_UNDO + 0x08, 0)
        self.uc.mem_write(G_UNDO + 0x0C, b"\x01\x00")
        self.wr32(G_UNDO + 0x10, 0)
        self.wr32(G_UNDO + 0x14, 10)
        # 砖表：默认全部可变形
        self.ntiles = 900
        tbl = self._bump(self.ntiles * 4)
        for i in range(self.ntiles):
            o = self._bump(0x2F0)
            self.uc.mem_write(o + 0x2E0, b"\x01")
            self.wr32(tbl + i * 4, o)
        self.tiletbl = tbl
        self.wr32(G_TILETBL, tbl)
        self.wr32(G_TILECNT, self.ntiles)
        # 工作记录 +0x00 打包坐标（阶段 1 只给菱形内的格子写）
        # 与格子 +0x44 = -1（没有 overlay）
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE,
                      (y << 16) | x)
            self.wr32(self.cell(x, y) + 0x44, 0xFFFFFFFF)
        for off in range(0, CELL_SIZE, 4):
            self.wr32(DUMMY + off, 0)
        self.wr32(DUMMY + 0x44, 0xFFFFFFFF)
        self.lat = []
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE, self._h_apply, begin=A_APPLY, end=A_APPLY)

    # ---- 环境布置 ----
    def set_morph(self, tile, v):
        o = self.rd32(self.tiletbl + tile * 4)
        self.uc.mem_write(o + 0x2E0, bytes([1 if v else 0]))

    def set_ramp(self, x, y, v):
        self.uc.mem_write(self.cell(x, y) + 0x11C, bytes([v & 0xFF]))

    def set_object(self, x, y, v):
        self.wr32(self.cell(x, y) + 0xE4, v)

    def set_overlay(self, x, y, v):
        self.wr32(self.cell(x, y) + 0x44, v)

    def set_height(self, x, y, v):
        self.uc.mem_write(self.work + (y * self.side + x) * WORK_SIZE + 0x08,
                          struct.pack("<d", v))

    # ---- 观测 ----
    def _h_apply(self, uc, address, size, ud):
        lat = self.rd32(G_LAT)
        stride = self.rd32(G_STRIDE)
        rows = self.rd32(G_ROWS)
        out = []
        for y in range(rows):
            for x in range(stride):
                e = lat + (y * stride + x) * 8
                hgt = struct.unpack("<i", self.uc.mem_read(e, 4))[0]
                lk = self.uc.mem_read(e + 4, 1)[0]
                dt = self.uc.mem_read(e + 5, 1)[0]
                if dt:
                    out.append((x, y, hgt, lk, dt))
        self.lat = out

    def _cellstate(self, addr):
        return (self.rd32(addr + 0x38),
                self.uc.mem_read(addr + 0x11A, 1)[0],
                struct.unpack("<b", self.uc.mem_read(addr + 0x11B, 1))[0],
                self.uc.mem_read(addr + 0x11C, 1)[0])

    def snapshot(self):
        out = {}
        for (x, y) in self._diamond():
            out[(x, y)] = self._cellstate(self.cell(x, y))
        return out

    def run(self, ruggedness):
        self.wr32(MAPSEED + 0x44, ruggedness & 0xFFFFFFFF)
        before = self.snapshot()
        self.rng_calls = 0
        self._call(A_HILLS, MAPSEED)
        after = self.snapshot()
        diff = sorted((k, after[k]) for k in after if after[k] != before[k])
        return diff, self._cellstate(DUMMY), self.rd32(DUMMY + 0x24), \
            self.rng_calls, self.lat


def lcg(seed):
    s = [seed & 0xFFFFFFFF]

    def f():
        s[0] = (s[0] * 1103515245 + 12345) & 0xFFFFFFFF
        return (s[0] >> 16) & 0x7FFF
    return f


# 每个用例返回 (tiles, heights, extra)：
#   tiles(x, y)   -> (tile, subtile, level, ramp)
#   heights(x, y) -> 工作记录 +0x08
#   extra(o)      -> 额外布置（非可变形砖、overlay、object）
def build(kind, w, h):
    r = lcg(0xBEEF ^ (w * 131 + h))

    def flat(x, y):
        return (CLEARTILE, 0, 0, 0)

    def zero(x, y):
        return 0.0

    def noise_heights(seed):
        g = lcg(seed)
        cache = {}

        def f(x, y):
            k = (x, y)
            if k not in cache:
                cache[k] = float(g() % 5 - 2)
            return cache[k]
        return f

    if kind == "flat":
        return flat, zero, None
    if kind == "one-bump":
        def one(x, y):
            return 2.0 if (x == w + 2 and y == w + 2) else 0.0
        return flat, one, None
    if kind == "noise":
        return flat, noise_heights(0x1234), None
    if kind == "noise-neg":
        g = lcg(0x9876)
        cache = {}

        def negh(x, y):
            k = (x, y)
            if k not in cache:
                cache[k] = float(g() % 3 - 2)   # -2..0，逼出 [0,180] 下界回退
            return cache[k]
        return flat, negh, None
    if kind == "anchored":
        # 岸砖与悬崖砖散落：① 会把邻格标成锚点，IsRaisable 随之失败、角点锁死
        def t(x, y):
            v = (x * 7 + y * 13) % 23
            if v == 0:
                return (SHORE + ((x + y) % 0x2A), 0, 0, 0)
            if v == 5:
                return (CLIFF + ((x * 3) % 0x28), 0, 0, 0)
            if v == 9:
                return (WATER, 0, 0, 0)
            return (CLEARTILE, 0, 0, 0)
        return t, noise_heights(0x2222), None
    if kind == "nonmorph":
        def t(x, y):
            return ((200 + (x + y) % 8, 0, 0, 0) if (x * 5 + y) % 11 == 0
                    else (CLEARTILE, 0, 0, 0))

        def extra(o):
            for i in range(8):
                o.set_morph(200 + i, False)
        return t, noise_heights(0x3333), extra
    if kind == "blocked":
        def extra(o):
            for (x, y) in o._diamond():
                if (x * 3 + y * 5) % 17 == 0:
                    o.set_object(x, y, 0x1234)
                elif (x * 3 + y * 5) % 17 == 8:
                    o.set_overlay(x, y, 7)
        return flat, noise_heights(0x4444), extra
    if kind == "prelevel":
        def t(x, y):
            # 斜向条带，相邻条带只差一级（差两级的话 §八 的 mx-mn >= 16
            # 会把整张图都判掉，用例就没内容了）
            lv = ((x + y) // 6) % 2
            return (CLEARTILE, 0, lv, 0)
        return t, noise_heights(0x5555), None
    if kind == "preramp":
        # 预置坡面：③ 建格网时四角起始高度不再是平的
        def t(x, y):
            v = (x * 11 + y * 3) % 7
            if v == 0:
                return (RAMPBASE + 0, 0, 1, 1)
            if v == 1:
                return (RAMPBASE + 4, 0, 1, 5)
            if v == 2:
                return (RAMPBASE + 8, 0, 2, 9)
            return (CLEARTILE, 0, 1, 0)
        return t, noise_heights(0x6666), None
    if kind == "rampblock":
        # 直接摆出 §九 那两种 2x2 拼法，逼收尾遍动手
        def t(x, y):
            bx, by = x - (w + 1), y - (w + 1)
            if 0 <= bx < 2 and 0 <= by < 2:
                # 顺序必须按 §九 那四个增量 (0,0) (1,0) (1,1) (0,1) 排
                ramps = {(0, 0): 5, (1, 0): 6, (1, 1): 7, (0, 1): 8}
                k = ramps[(bx, by)]
                return (RAMPBASE - 1 + k, 0, 0, k)
            bx, by = x - (w + 5), y - (w + 1)
            if 0 <= bx < 2 and 0 <= by < 2:
                ramps = [0x0B, 0x0C, 0x0A, 0x09]   # (0,0) (1,0) (0,1) (1,1)
                k = ramps[by * 2 + bx]
                return (RAMPBASE - 1 + k, 0, 0, k)
            # 菱形东端孤零零一块坡：往 (+1, y) 探邻格时会探到共享哑元，
            # 哑元的坐标随之被改写——这条副作用必须能复现
            if (x, y) == (w + 2 * h - 11, h):
                return (RAMPBASE + 4, 0, 0, 5)
            return (CLEARTILE, 0, 0, 0)
        return t, zero, None
    raise SystemExit("unknown kind " + kind)


CASES = [
    ("flat", 12, 10),
    ("one-bump", 12, 10),
    ("noise", 12, 10),
    ("noise-neg", 12, 10),
    ("anchored", 14, 12),
    ("nonmorph", 12, 10),
    ("blocked", 12, 10),
    ("prelevel", 12, 10),
    ("preramp", 14, 12),
    ("rampblock", 12, 10),
]


def main():
    out = []
    for (kind, w, h) in CASES:
        o = HillsOracle(w, h)
        tiles, heights, extra = build(kind, w, h)
        cells = sorted(o._diamond(), key=lambda p: (p[1], p[0]))
        for (x, y) in cells:
            t, st, lv, rp = tiles(x, y)
            o.set_cell(x, y, tile=t, subtile=st, level=lv)
            o.set_ramp(x, y, rp)
            o.set_height(x, y, heights(x, y))
        morph = []
        if extra:
            extra(o)
        for i in range(o.ntiles):
            if not o.uc.mem_read(o.rd32(o.tiletbl + i * 4) + 0x2E0, 1)[0]:
                morph.append(i)
        diff, dummy, dcoord, rng, lat = o.run(0)
        out.append("CASE %s %d %d -> changed=%d rng=%d lat=%d"
                   % (kind, w, h, len(diff), rng, len(lat)))
        for t in morph:
            out.append("  MORPH %d" % t)
        for (x, y) in cells:
            t, st, lv, rp = tiles(x, y)
            ov = o.rd32(o.cell(x, y) + 0x44)
            ob = o.rd32(o.cell(x, y) + 0xE4)
            hb = struct.pack("<d", heights(x, y))
            out.append("  IN %d %d %d %d %d %d %d %d %s"
                       % (x, y, t, st, lv, rp, ov - (1 << 32) if ov >> 31 else ov,
                          ob, hb[::-1].hex().upper()))
        for (x, y), st in diff:
            out.append("  OUT %d %d %d %d %d %d" % (x, y, st[0], st[1], st[2], st[3]))
        out.append("  DUMMY %d %d %d %d %d %d"
                   % (dummy[0], dummy[1], dummy[2], dummy[3],
                      dcoord & 0xFFFF, (dcoord >> 16) & 0xFFFF))
        for (x, y, hgt, lk, dt) in lat:
            out.append("  LAT %d %d %d %d %d" % (x, y, hgt, lk, dt))
    text = "\n".join(out) + "\n"
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as f:
            f.write("## 阶段 9 造山 0x5A35F0 黄金基准（生成脚本 "
                    "code/randmap/dump_hills_golden.py）\n")
            f.write("## Ruggedness 恒 0：0x5A2F50 直接返回，全程不取随机数；"
                    "高度场由脚本摆进 +0x08\n")
            f.write("## IN  x y tile subtile level ramp overlay object heightBits\n")
            f.write("## OUT x y tile subtile level ramp（只列变化过的格子）\n")
            f.write("## LAT x y height locked dirty（0x6B3850 入口处 dirty 的角点）\n")
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
