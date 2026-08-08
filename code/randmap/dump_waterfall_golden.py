# -*- coding: utf-8 -*-
u"""0x59E740 PlaceWaterfall 的整函数模拟执行。

河每走一步都可能触发它一次。它先在行进方向前方扫一片净空区，确认没人占着；
然后涂上游一片水、把整块台地抬高 4 层、再涂下游一片水（留在低处），
最后在两片水之间那道陡坎上贴四块瀑布砖。成功之后调用方的游标被直接推进
12 格，河道在这里断开重画。

环境沿用 OpsOracle（区域 oracle：摘掉 0x4865D0 恒真桩、静态 MapSeedClass
实例 0xABDFD8、带回收的 malloc/free），并在它上面补两样：

  1) 全局砖表 [0xA8ED2C] 要扩到能装下四组瀑布砖（温和：East 539、
     North 543、West 547、South 414）。SmoothOracle 建的那张表只有
     135 项，装不下 550，直接拿 real 剖面重建一张 800 项的。
  2) 0x57A0C0 岸线平滑按用例钉成恒真/恒假——跑真身会消耗随机数，
     把「每格 2 次拒绝采样」这条硬指标搅乱。

函数内部真的会跑 0x5A08D0 AngularGrow、0x5A0160 Dilate、0x5A0700
CollectBorderCells、0x57B440 PlaceTile、0x4A91B0 写落点、0x5657A0
GetCellAt、0x486380 未定砖谓词、0x5980C0 正态源——一个桩都不留。

⚠️ 本段取随机数：两片水面每格各 2 次拒绝采样（砖号 PickBelow(6) +
子格 PickBelow(4)），AngularGrow 自己还要取一堆。基准必须记取数个数。

基准记：
  IN  行 —— 输入（tile / subtile / level / +0x38 / +0x3C）
  OUT 行 —— 终态（同上五项）
  外加返回值、出参 ok、随机数调用次数、游标终值、共享哑元坐标 [0xABDC74]
"""
import io
import struct
import sys

import tile_profiles
from dump_region_ops_golden import OpsOracle
from dump_regions_golden import SEEDOBJ, sx
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE

A_WATERFALL = 0x59E740
A_SMOOTH = 0x57A0C0

# 温和气候的四组瀑布砖起始砖号（tileset_starts.txt / tile_geometry.txt）
WF = {0: ("wfnorth", 0xAA10A0, 543),
      2: ("wfeast", 0xAA073C, 539),
      4: ("wfsouth", 0xAA1050, 414),
      6: ("wfwest", 0xABB110, 547)}

TABLE_N = 800


class WaterfallOracle(OpsOracle):
    def __init__(self, w, h):
        OpsOracle.__init__(self, w, h)

        # ── 岸线平滑改成可切换的桩（SmoothOracle 早就把 PaintOracle 那个
        #    恒真桩摘掉了，所以这里只加不删）──
        self.smooth_ret = 1
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE, self._h_smooth,
                         begin=A_SMOOTH, end=A_SMOOTH)

        # ── 重建全局砖表：岸砖 42 块照旧（0x57A0C0 被钉掉了用不上，但
        #    Dilate/谓词那边还按砖号判水岸，留着不碍事），外加 16 块瀑布砖 ──
        old = self.rd32(0xA8ED2C)
        table = self._bump(4 * TABLE_N)
        for i in range(SHORE + 0x2A + 4):
            self.wr32(table + i * 4, self.rd32(old + i * 4))
        self.wr32(0xA8ED2C, table)
        for d, (group, glob, base) in WF.items():
            self.wr32(glob, base)
            for k in range(4):
                tw, th, blocks = tile_profiles.real_spec(group, k)
                obj = self.make_tile(base + k, tw, th, blocks)
                self.wr32(table + (base + k) * 4, obj)
        self.wr32(0x880990, 0)

    def _h_smooth(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, self.smooth_ret)
        self._ret(uc, 8)

    # ── 输入 ──
    def wterrain(self, kind, rect):
        u"""rect 是「河那一侧」的矩形，用来造出各种挡路的东西。"""
        rx, ry, rw, rh = rect
        for (x, y) in self._diamond():
            inr = rx <= x < rx + rw and ry <= y < ry + rh
            if kind == "undecided":
                tile, lvl = UNDECIDED, 0
            elif kind == "river":
                tile, lvl = (WATER if inr else UNDECIDED), 0
            elif kind == "blocked":
                # 净空区里塞一块普通砖：唯一的「此处放不下」出口
                tile, lvl = (1 if inr else UNDECIDED), 0
            elif kind == "levels":
                tile, lvl = UNDECIDED, ((x + y) // 5) % 4
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)

    def wowners(self, rect, owner, foe_rect=None, foe=0):
        rx, ry, rw, rh = rect
        for (x, y) in self._diamond():
            v = 0
            if rx <= x < rx + rw and ry <= y < ry + rh:
                v = owner
            elif foe_rect is not None:
                fx, fy, fw, fh = foe_rect
                if fx <= x < fx + fw and fy <= y < fy + fh:
                    v = foe
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x38,
                      v & 0xFFFFFFFF)

    def set_stamp(self, v):
        self.wr32(SEEDOBJ + 0x308, v & 0xFFFFFFFF)

    def snap(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)))
        return out

    def waterfall(self, blob, a, b, d, x0, y0):
        pa = self._bump(8)
        self.uc.mem_write(pa, struct.pack("<hh", a[0], a[1]))
        pb = self._bump(8)
        self.uc.mem_write(pb, struct.pack("<hh", b[0], b[1]))
        pok = self._bump(8)
        self.uc.mem_write(pok, b"\x5A")        # 哨兵：看得出有没有被写
        px = self._bump(8)
        self.uc.mem_write(px, struct.pack("<d", x0))
        py = self._bump(8)
        self.uc.mem_write(py, struct.pack("<d", y0))
        self.rng_calls = 0
        r = self._call(A_WATERFALL, SEEDOBJ,
                       (blob & 0xFFFFFFFF, pa, pb, d & 0xFFFFFFFF,
                        pok, px, py))
        return (r & 0xFF, self.uc.mem_read(pok, 1)[0], self.rng_calls,
                struct.unpack("<d", self.uc.mem_read(px, 8))[0],
                struct.unpack("<d", self.uc.mem_read(py, 8))[0])


# 名字, W, H, 地形 kind, 地形 rect, 归属 rect, blob(入参), stamp(+0x308),
# 第三方 rect, 第三方编号, a, b, dir, 岸线桩返回值, 游标初值
#
# ⚠️ 20x20 的菱形是 `x+y > 20`、`x+y <= 60`、`|x-y| < 20`，形心在 **(20,20)**，
# 不是 OpsOracle 那个偏在下尖角上的 (30,30)。河都摆在 (20,20) 一带，
# 否则净空区大半落到界外，什么都验不出来。
CASES = [
    # ── 四个朝向各跑一遍：河已经画到 (a,b)，下游那一侧归 stamp ──
    # dir 0 河往北：净空区 y ∈ [a.y-12, a.y)，台地矩形盖住 y >= a.y-4
    ("wf-n", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),
    # dir 4 河往南：a 在下、b 在上（净空区与矩形全用 b）
    ("wf-s", 20, 20, "river", (18, 8, 5, 8), (18, 8, 5, 8), 1, 1,
     None, 0, (22, 16), (18, 16), 4, 1, 100.0, 200.0),
    # dir 2 河往东：净空区 x ∈ [a.x+1, a.x+13)
    ("wf-e", 20, 20, "river", (6, 18, 8, 5), (6, 18, 8, 5), 1, 1,
     None, 0, (14, 18), (14, 22), 2, 1, 100.0, 200.0),
    # dir 6 河往西
    ("wf-w", 20, 20, "river", (27, 18, 8, 5), (27, 18, 8, 5), 1, 1,
     None, 0, (26, 22), (26, 18), 6, 1, 100.0, 200.0),

    # ── 净空扫描否决：净空区里有一块普通砖 → return 1 且 ok 保持 0 ──
    ("wf-blocked", 20, 20, "blocked", (18, 16, 2, 2), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),
    # 净空区里有别人的地盘 → 同样是那条出口（判的是 +0x38 != 0）
    ("wf-occupied", 20, 20, "undecided", (0, 0, 0, 0), (18, 24, 5, 8), 1, 1,
     (18, 16, 2, 2), 7, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 岸线平滑判失败：上游那片水已经涂下去了，且**不回滚** ──
    ("wf-nosmooth", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 0, 0, 100.0, 200.0),

    # ── 台地撞上第三方地盘：AngularGrow 或 Dilate 判 false ──
    ("wf-foe", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 1, 1,
     (14, 26, 12, 4), 9, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 入参 blob 与 +0x308 不同：涂水写 blob，台地/贴砖写 stamp ──
    ("wf-split-id", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 5, 1,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 有层高的地形：抬升 +4 与坎上的 -4 都验得出来 ──
    ("wf-levels", 20, 20, "levels", (0, 0, 0, 0), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 宽河 / 窄河：w2 的跨度奇偶决定交替贴砖那个循环走哪条支路 ──
    ("wf-n-wide", 20, 20, "river", (16, 24, 9, 8), (16, 24, 9, 8), 1, 1,
     None, 0, (16, 24), (24, 24), 0, 1, 100.0, 200.0),
    ("wf-n-narrow", 20, 20, "river", (19, 24, 2, 8), (19, 24, 2, 8), 1, 1,
     None, 0, (19, 24), (20, 24), 0, 1, 100.0, 200.0),
    ("wf-e-wide", 20, 20, "river", (6, 16, 8, 9), (6, 16, 8, 9), 1, 1,
     None, 0, (14, 16), (14, 24), 2, 1, 100.0, 200.0),
    ("wf-w-wide", 20, 20, "river", (27, 16, 8, 9), (27, 16, 8, 9), 1, 1,
     None, 0, (26, 24), (26, 16), 6, 1, 100.0, 200.0),
    ("wf-s-wide", 20, 20, "river", (16, 8, 9, 8), (16, 8, 9, 8), 1, 1,
     None, 0, (24, 16), (16, 16), 4, 1, 100.0, 200.0),

    # ── a == b：跨度 1 格，两头的砖挤在一起 ──
    ("wf-n-dot", 20, 20, "river", (20, 24, 1, 8), (20, 24, 1, 8), 1, 1,
     None, 0, (20, 24), (20, 24), 0, 1, 100.0, 200.0),
    ("wf-e-dot", 20, 20, "river", (6, 20, 8, 1), (6, 20, 8, 1), 1, 1,
     None, 0, (14, 20), (14, 20), 2, 1, 100.0, 200.0),

    # ── 贴着菱形边：净空区大半在界外，两片水都只涂到一部分 ──
    ("wf-n-edge", 20, 20, "river", (6, 10, 5, 8), (6, 10, 5, 8), 1, 1,
     None, 0, (6, 10), (10, 10), 0, 1, 100.0, 200.0),

    # ── dir 不在 0/2/4/6：所有矩形保持 0，AngularGrow 拿一个空矩形跑 ──
    # ⚠️ dir 9 的 `dir/2 = 4` 会读到两张表之外的栈槽（原版 UB）。
    #    这里实测两个增量都是 0，基准把这一点钉住。
    ("wf-dir1", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 1, 1, 100.0, 200.0),
    ("wf-dir9", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), 1, 1,
     None, 0, (18, 24), (22, 24), 9, 1, 100.0, 200.0),

    # ── 编号是负数：涂水与认领都用它，`owner != 0` 那条判据要非 1 编号 ──
    ("wf-negid", 20, 20, "river", (18, 24, 5, 8), (18, 24, 5, 8), -3, -3,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 没有任何格子归 stamp：上游那片水自己就是 AngularGrow 的全部种子 ──
    ("wf-noseed", 20, 20, "undecided", (0, 0, 0, 0), (0, 0, 0, 0), 1, 1,
     None, 0, (18, 24), (22, 24), 0, 1, 100.0, 200.0),

    # ── 小图：菱形只有 (x+y ∈ (10,30]，|x-y| < 10)，河横穿整张图 ──
    ("wf-small", 10, 10, "river", (8, 12, 5, 6), (8, 12, 5, 6), 1, 1,
     None, 0, (8, 12), (12, 12), 0, 1, 0.0, 0.0),
]

HEAD = (u"# 0x59E740 PlaceWaterfall 整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_waterfall_golden.py\n"
        u"# IN/OUT 行 = x y tile subtile level owner queued\n"
        u"# WaterSet=314 ShorePieces=89"
        u" WaterfallNorth=543 East=539 South=414 West=547（温和）\n"
        u"# 瀑布砖几何取自 memory/data/rmg/tile_geometry.txt 的 real 剖面\n"
        u"# ⚠️ 0x57A0C0 岸线平滑按 smooth= 钉成恒真/恒假，不消耗随机数\n")


def main():
    out = []
    for (name, w, h, terr, trect, orect, blob, stamp, foe_rect, foe,
         a, b, d, smooth, x0, y0) in CASES:
        o = WaterfallOracle(w, h)
        o.smooth_ret = smooth
        o.wterrain(terr, trect)
        o.wowners(orect, stamp, foe_rect, foe)
        o.set_stamp(stamp)
        before = o.snap()
        ret, ok, calls, xf, yf = o.waterfall(blob, a, b, d, x0, y0)
        after = o.snap()
        dummy = o.rd32(0xABDC50 + 0x24)
        head = ("CASE %s %d %d terrain=%s trect=%d,%d,%d,%d"
                " orect=%d,%d,%d,%d blob=%d stamp=%d foe=%s,%d"
                " a=%d,%d b=%d,%d dir=%d smooth=%d xy0=%g,%g"
                " -> ret=%d ok=%d rng=%d x=%g y=%g dummy=%d,%d"
                % (name, w, h, terr, trect[0], trect[1], trect[2], trect[3],
                   orect[0], orect[1], orect[2], orect[3], blob, stamp,
                   "none" if foe_rect is None
                   else "%d,%d,%d,%d" % foe_rect, foe,
                   a[0], a[1], b[0], b[1], d, smooth, x0, y0,
                   ret, ok, calls, xf, yf,
                   dummy & 0xFFFF, (dummy >> 16) & 0xFFFF))
        out.append(head)
        for k in sorted(before):
            out.append("  IN %d %d %d %d %d %d %d" % (k + before[k]))
        for k in sorted(after):
            out.append("  OUT %d %d %d %d %d %d %d" % (k + after[k]))
        print(head)
        sys.stdout.flush()

    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/waterfall_golden.txt")
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD)
        f.write(u"\n".join(out) + u"\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
