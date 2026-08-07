# -*- coding: utf-8 -*-
u"""\u5cb8\u7ebf\u5e73\u6ed1 0x57A0C0 \u7684\u6574\u51fd\u6570\u6a21\u62df\u6267\u884c\u3002

\u4e00\u6b21\u8c03\u7528\u5c31\u628a\u56db\u904d\u626b\u5168\u56fe\uff08\u4fb5\u8680 0x57A430 / \u6e05\u7406 0x57A320 /
\u76f4\u8fb9 0x57ACF0(...,1) / \u62d0\u89d2 0x57ACF0(...,2)\uff09\u8fde\u540c\u5b83\u4eec\u5e95\u4e0b\u7684\u63a9\u7801
0x57B210 \u4e0e\u8d34\u7816\u5668 0x57B440 \u5168\u8dd1\u4e00\u904d\u3002

\u73af\u5883\u5728 PlaceOracle \u57fa\u7840\u4e0a\u591a\u6446\u4e09\u6837\uff1a
  1) \u628a PaintOracle \u5bf9 0x57A0C0 \u7684\u201c\u6052\u771f\u201d\u6869\u6458\u6389\uff08hook_del\uff09\uff0c
     \u5426\u5219\u8dd1\u7684\u662f\u6869\u800c\u4e0d\u662f\u771f\u8eab\u3002
  2) \u5168\u5c40\u7816\u8868 [0xA8ED2C]\uff1a0x57B12D \u4ee5 TileTypes[ShorePieces + i - 1] \u53d6\u7816\uff0c
     \u540e\u7eed\u76f4\u63a5 mov eax,[ecx] \u8c03\u865a\u8868\u2014\u2014\u6ca1\u6709\u7a7a\u6307\u9488\u4fdd\u62a4\uff0c
     \u6240\u4ee5 42 \u5757\u5cb8\u7816\u5fc5\u987b\u5168\u90e8\u5b9e\u4f53\u5316\u3002\u5c3a\u5bf8\u7528\u786e\u5b9a\u6027\u5408\u6210\u89c4\u5219
     \uff08shore_tile_size\uff09\uff0c\u4e0e C++ \u5bf9\u62cd\u90a3\u8fb9\u9010\u5b57\u4e00\u81f4\u3002
  3) 0x4A91B0\uff08\u5199\u843d\u70b9\u5750\u6807\uff09\u8981\u8d70 RMG \u90a3\u4e00\u652f\uff1aMap->+0x117C = 0\uff0c
     \u4e14\u843d\u70b9\u6c38\u8fdc\u4e0d\u7b49\u4e8e [0x8A03F8]/[0x8A03FA]\uff08\u586b\u54e8\u5175\u503c 0x7FFF\uff09\u3002

\u5de5\u4f5c\u6570\u7ec4\u6bcf\u6761\u8bb0\u5f55\u628a +0x4A \u7f6e 1\uff08\u6784\u9020\u5668 0x58BE0E \u7684\u884c\u4e3a\uff09\uff0c
\u4e0d\u7f6e\u7684\u8bdd 0x57B2CC \u4f1a\u628a\u6bcf\u4e2a\u63a9\u7801\u6253\u6210 0\uff0c\u56db\u904d\u626b\u63cf\u5168\u7a7a\u8f6c\u3002

\u57fa\u51c6\u8bb0\uff1a\u8fd4\u56de\u503c\u3001\u968f\u673a\u6570\u8c03\u7528\u6b21\u6570\u3001\u4ee5\u53ca\u6bcf\u683c\u7684
(tile, subtile, level, workBlob) \u7ec8\u6001\u3002
"""
import struct
import sys

import tile_profiles
from dump_place_tile_golden import PlaceOracle
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_SMOOTH = 0x57A0C0

WATER = 314        # temperate WaterSet
SHORE = 89         # temperate ShorePieces
UNDECIDED = 0      # land init state: undecided tile


def shore_tile_size(k, profile="mix"):
    u"""第 k 块岸砖（k = tile_number - ShorePieces）的尺寸。

    三套剖面（mix / unit / real）的定义在 tile_profiles.py，
    C++ 对拍侧的 test/tile_geometry_fixture.h 读同一份真值表。
    """
    return tile_profiles.shore_spec(k, profile)[:2]


class SmoothOracle(PlaceOracle):
    def __init__(self, w, h, seed=1, profile="mix"):
        PlaceOracle.__init__(self, w, h, seed)
        self.uc.hook_del(self.h_smooth)

        # 落点偏移表 0xABDB64 在文件里是全零，由 CRT 静态初始化器
        # 0x57A9C0-0x57ACE8 在运行期填出。不跑它的话每块砖都会落在
        # 中心格上，基准整个作废。
        self._run_range(0x57A9C0, 0x57ACE8)

        self.wr32(0x87F7E8 + 0x117C, 0)
        self.wr32(0x87F7E8 + 0x1174, 0)
        self.uc.mem_write(0x8A03F8, struct.pack("<hh", 0x7FFF, 0x7FFF))
        self.wr32(0x88098C, 0)
        self.wr32(0x880990, 0)

        n = SHORE + 0x2A + 4
        table = self._bump(4 * n)
        self.wr32(0xA8ED2C, table)
        for k in range(0x2A):
            tw, th, blocks = tile_profiles.shore_spec(k, profile)
            obj = self.make_tile(SHORE + k, tw, th, blocks)
            self.wr32(table + (SHORE + k) * 4, obj)
        self.wr32(0x880990, 0)

        for i in range(self.side * self.side):
            r = self.work + i * WORK_SIZE
            self.uc.mem_write(r + 0x4A, b"\x01")
            self.wr32(r + 0x40, 0xFFFFFFFF)

    def _run_range(self, start, end):
        from unicorn.x86_const import UC_X86_REG_ESP
        self.uc.reg_write(UC_X86_REG_ESP, 0x20000000 + 0x100000 - 0x8000)
        self.uc.emu_start(start, end)

    def seed_map(self, land):
        for (x, y) in self._diamond():
            self.set_cell(x, y, tile=(UNDECIDED if (x, y) in land else WATER),
                          subtile=0, level=0)

    def run(self, blob_id, flag):
        self.rng_calls = 0
        ret = self._call(A_SMOOTH, 0x87F7E8,
                         (blob_id & 0xFFFFFFFF, 1 if flag else 0))
        return ret & 0xFF, self.rng_calls, self.snapshot()


# ---- terrain generators (mirrored verbatim in the C++ test) ----
def lcg_land(diamond, seed, threshold):
    s = seed & 0xFFFFFFFF
    out = set()
    for c in diamond:
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        if ((s >> 16) & 0x7FFF) < threshold:
            out.add(c)
    return out


def disc_land(diamond, w, h, radius):
    cx, cy = w + h // 2, h + w // 2
    return set((x, y) for (x, y) in diamond
               if (x - cx) ** 2 + (y - cy) ** 2 <= radius * radius)


def isthmus_land(diamond, w, h):
    cy = h + w // 2
    land = set()
    for (x, y) in diamond:
        if abs(y - cy) <= 3 and (w + 2 <= x <= w + 8 or w + 14 <= x <= w + 20):
            land.add((x, y))
    for x in range(w + 9, w + 14):
        if (x, cy) in diamond:
            land.add((x, cy))
    return land


def dots_land(diamond, step):
    u"""陆地一片，只敲出孤立的水点。

    孤点四周的斜向邻格会拿到**只有一位对角**的掩码，
    那是 0x57B0ED 那四条纯对角分支（i = 0x21..0x28）唯一的入口。
    """
    return set(c for c in diamond
               if not (c[0] % step == 1 and c[1] % step == 1))


def build_land(kind, diamond, w, h):
    if kind == "isthmus":
        return isthmus_land(diamond, w, h)
    tag, rest = kind.split(":", 1)
    if tag == "disc":
        return disc_land(diamond, w, h, int(rest))
    if tag == "dots":
        return dots_land(diamond, int(rest))
    seed, thr = rest.split(":")
    return lcg_land(diamond, int(seed, 0), int(thr))


# 名字, W, H, 种子, 地形, blob, flag, 砖尺寸剖面
CASES = [
    ("disc-flag1", 20, 20, 0x1234, "disc:7", 5, 1, "mix"),
    ("disc-flag0", 20, 20, 0x1234, "disc:7", 5, 0, "mix"),
    ("noise55-flag1", 20, 20, 0xC0FFEE, "lcg:0xC0FFEE:18022", 5, 1, "mix"),
    ("noise30-flag1", 20, 20, 0x5A5A, "lcg:0x5A5A:9830", 5, 1, "mix"),
    ("noise55-flag0", 20, 20, 0xC0FFEE, "lcg:0xC0FFEE:18022", 5, 0, "mix"),
    ("isthmus-flag1", 16, 14, 0x777, "isthmus", 3, 1, "mix"),
    ("isthmus-flag0", 16, 14, 0x777, "isthmus", 3, 0, "mix"),
    ("allland", 14, 12, 0x9, "lcg:0x9:32767", 2, 1, "mix"),
    ("allwater", 14, 12, 0x9, "lcg:0x9:0", 2, 1, "mix"),
    ("noise-big", 26, 22, 0xBEEF, "lcg:0xBEEF:16384", 7, 1, "mix"),
    # 孤水点：专门压 0x57B0ED 那四条纯对角分支
    ("dots4", 20, 20, 0x11, "dots:4", 4, 1, "mix"),
    ("dots5", 20, 20, 0x22, "dots:5", 4, 1, "mix"),
    ("dots3", 18, 16, 0x33, "dots:3", 4, 1, "mix"),
    # 再撑几个不同密度/种子的噪声，把拐角分支扫全
    ("noise70", 22, 18, 0x1111, "lcg:0x1111:22937", 6, 1, "mix"),
    ("noise85", 22, 18, 0x2222, "lcg:0x2222:27853", 6, 1, "mix"),
    ("noise45", 22, 18, 0x3333, "lcg:0x3333:14746", 6, 1, "mix"),
    # 全 1x1 砖：每块只盖一格，不会把后面的格子提前遮住，
    # 于是 0x57B0ED 那四条纯对角分支才能真正跑到
    ("dots5-unit", 20, 20, 0x44, "dots:5", 4, 1, "unit"),
    ("dots3-unit", 18, 16, 0x55, "dots:3", 4, 1, "unit"),
    ("noise55-unit", 20, 20, 0xC0FFEE, "lcg:0xC0FFEE:18022", 5, 1, "unit"),
    ("noise70-unit", 22, 18, 0x1111, "lcg:0x1111:22937", 6, 1, "unit"),
    ("disc-unit", 20, 20, 0x1234, "disc:7", 5, 1, "unit"),
    # 真实 .tmp 尺寸：岸砖有 2x3 / 3x2 / 6x4 / 9x5 这些合成剖面给不出的大块，
    # 一块砖能盖住 45 格，贴砖器的"整块否定"与遮挡关系跟合成剖面完全不同
    ("disc-real", 20, 20, 0x1234, "disc:7", 5, 1, "real"),
    ("noise55-real", 20, 20, 0xC0FFEE, "lcg:0xC0FFEE:18022", 5, 1, "real"),
    ("isthmus-real", 16, 14, 0x777, "isthmus", 3, 1, "real"),
    ("dots5-real", 20, 20, 0x44, "dots:5", 4, 1, "real"),
    ("noise70-real", 22, 18, 0x1111, "lcg:0x1111:22937", 6, 0, "real"),
]


HEAD = (u"# \u5cb8\u7ebf\u5e73\u6ed1 0x57A0C0 \u6574\u51fd\u6570\u6a21\u62df\u6267\u884c\u57fa\u51c6\n"
        u"# \u751f\u6210\u811a\u672c code/randmap/dump_shore_smooth_golden.py\n"
        u"# LAND \u884c = \u521d\u6001\u9646\u5730\uff08\u5176\u4f59\u83f1\u5f62\u5185\u683c\u5b50\u4e3a\u6c34 tile=314\uff09\n"
        u"# CELL \u884c = \u7ec8\u6001 x y tile subtile level workBlob\n")


def main():
    out = []
    for (name, w, h, seed, kind, blob, flag, profile) in CASES:
        o = SmoothOracle(w, h, seed, profile)
        diamond = o._diamond()
        land = build_land(kind, diamond, w, h)
        o.seed_map(land)
        ret, calls, after = o.run(blob, flag)
        head = ("CASE %s %d %d seed=0x%08X %s blob=%d flag=%d tiles=%s"
                " -> ret=%d rng=%d land=%d"
                % (name, w, h, seed, kind, blob, flag, profile,
                   ret, calls, len(land)))
        out.append(head)
        out.append("  LAND " + " ".join("%d:%d" % c for c in sorted(land)))
        for (x, y) in sorted(after):
            t, st, lv, wb = after[(x, y)]
            out.append("  CELL %d %d %d %d %d %d"
                       % (x, y, t, st, lv, wb & 0xFFFFFFFF))
        print(head)
        sys.stdout.flush()
    text = "\n".join(out) + "\n"
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/shore_smooth_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write(text)
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
