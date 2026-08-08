# -*- coding: utf-8 -*-
u"""0x59C920 WaterBlob（往陆地里涂水 = 湖）的整函数模拟执行。

阶段 2 主涂块器 0x59BBC0 的镜像：同样是最小堆扩散，但优先级里没有正态项、
没有会漂移的引力中心，只有「10*U(0,1) + 0.5*到起点距离 - 0.02*已涂格数」。

环境沿用 OpsOracle（区域 oracle：摘掉 0x4865D0 恒真桩、ClearToGreenLat=494、
静态 MapSeedClass 实例 0xABDFD8、带回收的 malloc/free）。函数内部真的会调
0x5A0410 Erode、0x5A0160 Dilate、0x5A0700 CollectBorderCells、0x5657A0
GetCellAt、0x486380 未定砖谓词、0x5980C0 正态源——一个桩都不留。

⚠️ 唯一留桩的是 0x57A0C0 岸线平滑：它自己已经有一份 17 万字节的基准
（shore_smooth_golden.txt），在这里跑真身只会把这份基准变成「岸线平滑的
第二份基准」，且它会消耗随机数、把取数个数搅成一锅。所以按用例把它钉成
恒真或恒假，两条出口都覆盖到。

⚠️ 本段取随机数：起点拒绝采样每轮 2 次、块大小的截尾正态每轮 1 次
（正态源自己一次调用取 2 个）、每接受一个邻格 1 次。基准必须记取数个数。

基准记：
  IN  行 —— 输入（tile / subtile / level / +0x38 / +0x3C / +0x44 / +0x4B）
  OUT 行 —— 终态（同上七项）
  外加返回值、随机数调用次数、+0x304 终值、共享哑元坐标 [0xABDC74]
"""
import io
import struct
import sys

from dump_region_ops_golden import OpsOracle
from dump_regions_golden import SEEDOBJ, sx
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE

A_WATER_BLOB = 0x59C920
A_SMOOTH = 0x57A0C0


class WaterOracle(OpsOracle):
    def __init__(self, w, h):
        OpsOracle.__init__(self, w, h)
        # 岸线平滑挂一个「可切换」的桩。⚠️ SmoothOracle 早就把 PaintOracle
        # 那个恒真桩摘掉了（它要跑 0x57A0C0 的真身），所以这里只加不删。
        self.smooth_ret = 1
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE, self._h_smooth,
                         begin=A_SMOOTH, end=A_SMOOTH)

    def _h_smooth(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, self.smooth_ret)
        self._ret(uc, 8)

    # ── 输入：地形与归属。中心取菱形的**真正**形心（x = y = (W+H)/2），
    #    不是 OpsOracle 那个偏在尖角上的 (W+H/2, H+W/2)。
    def center(self):
        return ((self.w + self.h) // 2, (self.w + self.h) // 2)

    def wterrain(self, kind):
        cx, cy = self.center()
        for (x, y) in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "undecided":
                tile, lvl = UNDECIDED, 0
            elif kind == "solid":
                tile, lvl = 1, 0        # 全是普通砖：什么都涂不进去
            elif kind == "lake":
                tile, lvl = (WATER if d < 4 else UNDECIDED), 0
            elif kind == "wall":
                tile = 1 if 6 <= d <= 7 else UNDECIDED
                lvl = 0
            elif kind == "levels":
                tile, lvl = UNDECIDED, ((x + y) // 4) % 5
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)

    def wowners(self, kind):
        cx, cy = self.center()
        for (x, y) in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "none":
                v = 0
            elif kind == "disk":
                v = 1 if d < 5 else 0        # 中心一块别人的地盘
            elif kind == "ring":
                v = 1 if 8 <= d <= 9 else 0  # 一圈别人的地盘
            elif kind == "half":
                v = 1 if x > cx else 0
            elif kind == "all1":
                v = 1
            else:
                raise ValueError(kind)
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x38, v)

    def dirty44(self, v):
        u"""预先给 +0x44 灌一个非零残留：start != (0,0) 那条会把它全刷成 1，
        start == (0,0) 那条会先全清再按 Erode 的结果重摆——两条都得验。"""
        for (x, y) in self._diamond():
            self.uc.mem_write(
                self.work + (y * self.side + x) * WORK_SIZE + 0x44,
                struct.pack("B", v))

    def seedfields(self, water, painted, blob, level):
        self.wr32(SEEDOBJ + 0x4C, water & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x304, painted & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x308, blob & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x30C, level & 0xFFFFFFFF)

    def snap(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)),
                           self.uc.mem_read(r + 0x44, 1)[0],
                           self.uc.mem_read(r + 0x4B, 1)[0])
        return out

    def water_blob(self, start):
        ps = self._bump(8)
        self.uc.mem_write(ps, struct.pack("<hh", start[0], start[1]))
        self.rng_calls = 0
        r = self._call(A_WATER_BLOB, SEEDOBJ, (ps,))
        return r & 0xFF, self.rng_calls, sx(self.rd32(SEEDOBJ + 0x304))


# 名字, W, H, 地形, 归属, +0x44 脏值, WaterAmount, painted, blob, level30c,
# start, 岸线平滑桩的返回值
CASES = [
    # ── 预算那三条早退 ──
    # WaterAmount = 0：eax 保持为 0（寄存器残值），budget = -painted 必然失败
    ("w-wa0", 12, 12, "undecided", "none", 0, 0, 0, 1, 4, (12, 12), 1),
    # budget 恰好 <= 0x4B：111 - 40 = 71
    ("w-tight-fail", 12, 12, "undecided", "none", 0, 10, 40, 1, 4, (12, 12), 1),
    # budget = 111 - 30 = 81，刚够往下走
    ("w-tight-ok", 12, 12, "undecided", "none", 0, 10, 30, 1, 4, (12, 12), 1),

    # ── start != (0,0)：跳过禁涂带预处理与成功收尾 ──
    ("w-mid", 12, 12, "undecided", "none", 0, 100, 0, 1, 4, (12, 12), 1),
    # 脏 +0x44 会被整块刷成 1
    ("w-mid-dirty44", 12, 12, "undecided", "none", 7, 100, 0, 1, 4,
     (12, 12), 1),
    # 撞上中心那块别人的地盘 → ok = false → 整块回滚
    ("w-foe", 12, 12, "undecided", "disk", 0, 100, 0, 2, 4, (16, 12), 1),
    # 别人的地盘围成一圈：湖在圈里长，长到贴上就失败
    ("w-ring", 14, 14, "undecided", "ring", 0, 100, 0, 2, 4, (14, 14), 1),
    # 全图普通砖：起点直接被涂（主循环不查砖），四邻全不合格 → 只涂 1 格 →
    # 格数不达标 → 回滚
    ("w-solid", 12, 12, "solid", "none", 0, 100, 0, 1, 4, (12, 12), 1),
    # 中心已经是水：起点落在水砖上，同样是「主循环不查砖」那条
    ("w-onlake", 12, 12, "lake", "none", 0, 100, 0, 1, 4, (12, 12), 1),
    # 砌死的一圈：湖被围住，收编堆那一遍会撞上不合格的格子 → 失败
    ("w-wall", 12, 12, "wall", "none", 0, 100, 0, 1, 4, (12, 12), 1),
    # 有层高的地形：回滚时 +0x11B 会被统一压成 level30c，验得出来
    ("w-levels", 12, 12, "levels", "disk", 0, 100, 0, 2, 7, (16, 12), 1),
    # 起点贴着菱形尖角：四邻大半在界外
    ("w-corner", 12, 12, "undecided", "none", 0, 100, 0, 1, 4, (1, 12), 1),
    # 编号是负数：`owner != 0 && owner != blob` 那条判据要用非 1 编号验
    ("w-blobneg", 12, 12, "undecided", "none", 0, 100, 0, -3, 4, (12, 12), 1),
    # 大图 + 大预算：主循环跑满 this_budget 而不是把堆抽干
    ("w-big", 20, 20, "undecided", "none", 0, 100, 0, 1, 4, (20, 20), 1),
    # 预算大到 this_budget 超过整张图的格数：主循环靠堆空退出
    ("w-overbudget", 10, 10, "undecided", "none", 0, 200, 0, 1, 4, (10, 10), 1),

    # ── start == (0,0)：随机起点 + 禁涂带 + 成功收尾 ──
    ("w-rnd-none", 14, 14, "undecided", "none", 0, 100, 0, 1, 4, (0, 0), 1),
    # 中心一块别人的地盘 → Erode(0,2,-2) 在它外面留两格禁涂带
    ("w-rnd-disk", 14, 14, "undecided", "disk", 0, 100, 0, 2, 4, (0, 0), 1),
    # 半张图归别人：禁涂带沿着那条直边铺开
    ("w-rnd-half", 14, 14, "undecided", "half", 0, 100, 0, 2, 4, (0, 0), 1),
    # 全图归别人：CollectBorderCells(0) 是空的 → 一格都不剥，但 owner != 0
    # 也 != blob → +0x44 全 0 → 200 次都抽不到合法起点
    ("w-rnd-all1", 12, 12, "undecided", "all1", 0, 100, 0, 2, 4, (0, 0), 1),
    # 全图普通砖：合法起点要求未定砖 → 200 次全废
    ("w-rnd-solid", 12, 12, "solid", "none", 0, 100, 0, 1, 4, (0, 0), 1),
    # 岸线平滑判失败 → 涂完了照样整块回滚
    ("w-rnd-nosmooth", 14, 14, "undecided", "none", 0, 100, 0, 1, 4,
     (0, 0), 0),
    # 有层高 + 禁涂带 + 成功收尾（Dilate 那一遍 + 刷绿）
    ("w-rnd-levels", 14, 14, "levels", "disk", 0, 100, 0, 2, 4, (0, 0), 1),
    # 脏 +0x44：start == (0,0) 这条会先全清，脏值不该漏进来
    ("w-rnd-dirty44", 14, 14, "undecided", "disk", 9, 100, 0, 2, 4, (0, 0), 1),
    # 中心已经是湖：随机起点会绕开水砖
    ("w-rnd-lake", 14, 14, "lake", "none", 0, 100, 0, 1, 4, (0, 0), 1),
    # painted 非 0：预算被前几块吃掉一部分，成功时还要把格数累加回去
    ("w-rnd-painted", 14, 14, "undecided", "none", 0, 100, 60, 1, 4,
     (0, 0), 1),
]

HEAD = (u"# 0x59C920 WaterBlob（湖）整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_water_blob_golden.py\n"
        u"# IN/OUT 行 = x y tile subtile level owner queued allow flag4b\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493 ClearToGreenLat=494\n"
        u"# ⚠️ 0x57A0C0 岸线平滑按 smooth= 钉成恒真/恒假，不消耗随机数\n")


def emit(out, head, before, after):
    out.append(head)
    for k in sorted(before):
        out.append("  IN %d %d %d %d %d %d %d %d %d" % (k + before[k]))
    for k in sorted(after):
        out.append("  OUT %d %d %d %d %d %d %d %d %d" % (k + after[k]))
    print(head)
    sys.stdout.flush()


def main():
    out = []
    for (name, w, h, terr, own, d44, wa, painted, blob, lvl, start, smooth) \
            in CASES:
        o = WaterOracle(w, h)
        o.smooth_ret = smooth
        o.wterrain(terr)
        o.wowners(own)
        o.dirty44(d44)
        o.seedfields(wa, painted, blob, lvl)
        before = o.snap()
        ret, calls, total = o.water_blob(start)
        after = o.snap()
        dummy = o.rd32(0xABDC50 + 0x24)
        emit(out, "CASE %s %d %d terrain=%s owners=%s dirty44=%d water=%d"
                  " painted=%d blob=%d level30c=%d start=%d,%d smooth=%d"
                  " -> ret=%d rng=%d total=%d dummy=%d,%d"
                  % (name, w, h, terr, own, d44, wa, painted, blob, lvl,
                     start[0], start[1], smooth, ret, calls, total,
                     dummy & 0xFFFF, (dummy >> 16) & 0xFFFF),
             before, after)

    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/water_blob_golden.txt")
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD)
        f.write(u"\n".join(out) + u"\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
