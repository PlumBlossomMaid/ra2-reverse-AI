# -*- coding: utf-8 -*-
u"""0x59D510 River + 0x59C580 AddRiversAndLakes 的整函数模拟执行。

River 是递归的（~1% 概率生支流），内部还会调：
  0x59C920 WaterBlob（河口挖湖）
  0x59E740 PlaceWaterfall（可选瀑布）
  0x57A0C0 SmoothShoreline（桩，可切换真/假）
  0x5A0160 Dilate / 0x5A08D0 AngularGrow（真身）

环境沿用 WaterfallOracle（已建好瀑布砖表 + SmoothShoreline 桩）。

基准记：
  IN/OUT 行 = x y tile subtile level owner queued allow flag4b
  外加 ret rng painted blob level30c dummy
"""
import io
import struct
import sys

from dump_waterfall_golden import WaterfallOracle
from dump_regions_golden import SEEDOBJ, sx
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE

A_RIVER  = 0x59D510
A_ADD_RL = 0x59C580
DUMMY_COORD = 0xABDC74   # 共享哑元坐标


class RiverOracle(WaterfallOracle):
    u"""WaterfallOracle + 河流调用入口。"""

    def __init__(self, w, h):
        WaterfallOracle.__init__(self, w, h)
        # 设置 FPU 控制字为原版的 0x0E7F（PC=53 + RC=chop）
        from unicorn.x86_const import UC_X86_REG_FPCW
        self.uc.reg_write(UC_X86_REG_FPCW, 0x0E7F)

    def center(self):
        return ((self.w + self.h) // 2, (self.w + self.h) // 2)

    def rterrain(self, kind):
        cx, cy = self.center()
        for x, y in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "undecided":
                tile, lvl = UNDECIDED, 0
            elif kind == "solid":
                tile, lvl = 1, 0
            elif kind == "lake":
                tile, lvl = (WATER if d < 4 else UNDECIDED), 0
            elif kind == "levels":
                tile, lvl = UNDECIDED, ((x + y) // 4) % 5
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)

    def rowners(self, kind):
        cx, cy = self.center()
        for x, y in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "none":
                v = 0
            elif kind == "disk":
                v = 1 if d < 5 else 0
            elif kind == "ring":
                v = 1 if 8 <= d <= 9 else 0
            elif kind == "half":
                v = 1 if x > cx else 0
            elif kind == "all1":
                v = 1
            else:
                raise ValueError(kind)
            self.wr32(
                self.work + (y * self.side + x) * WORK_SIZE + 0x38,
                v & 0xFFFFFFFF)

    def seedfields(self, map_type, water, painted, blob, level, allow_wf):
        self.wr32(SEEDOBJ + 0x3C,  map_type  & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x4C,  water     & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x304, painted   & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x308, blob      & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x30C, level     & 0xFFFFFFFF)
        self.wr32(SEEDOBJ + 0x310, allow_wf  & 0xFFFFFFFF)

    def snap(self):
        out = {}
        for x, y in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (
                self.rd32(c + 0x38),
                self.uc.mem_read(c + 0x11A, 1)[0],
                struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                sx(self.rd32(r + 0x38)),
                sx(self.rd32(r + 0x3C)),
                self.uc.mem_read(r + 0x44, 1)[0],
                self.uc.mem_read(r + 0x4B, 1)[0],
            )
        return out

    def river(self, start, angle=0.0, is_branch=0):
        """调用 0x59D510 River(__thiscall: ecx=SEEDOBJ, start*, angle_double, is_branch)"""
        ps = self._bump(8)
        self.uc.mem_write(ps, struct.pack("<hh", start[0], start[1]))
        # double angle 在 x86 cdecl/thiscall 中占 8 字节栈空间，
        # 低4字节在低地址（_call 逆序压栈：先压高字，再压低字）
        ang_lo, ang_hi = struct.unpack("<II", struct.pack("<d", angle))
        self.rng_calls = 0
        r = self._call(A_RIVER, SEEDOBJ,
                       (ps, ang_lo, ang_hi, is_branch & 0xFF))
        ret     = r & 0xFF
        painted = sx(self.rd32(SEEDOBJ + 0x304))
        blob    = sx(self.rd32(SEEDOBJ + 0x308))
        level   = sx(self.rd32(SEEDOBJ + 0x30C))
        dummy   = self.rd32(DUMMY_COORD)
        return ret, self.rng_calls, painted, blob, level, dummy

    def add_rivers_and_lakes(self):
        """调用 0x59C580 AddRiversAndLakes(__thiscall: ecx=SEEDOBJ)"""
        self.rng_calls = 0
        self._call(A_ADD_RL, SEEDOBJ, ())
        painted = sx(self.rd32(SEEDOBJ + 0x304))
        blob    = sx(self.rd32(SEEDOBJ + 0x308))
        level   = sx(self.rd32(SEEDOBJ + 0x30C))
        dummy   = self.rd32(DUMMY_COORD)
        return self.rng_calls, painted, blob, level, dummy


# 名字, W, H, terrain, owners,
# map_type, water, painted, blob, level, allow_wf,
# start, angle, is_branch, smooth
CASES_RIVER = [
    # ── start=={0,0}: 随机从地图边缘入场 ──
    # 基本通路：water=50 → 宽1~3，有峡谷检测
    ("r-rnd-basic",    20, 20, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (0, 0), 0.0, 0, 1),
    # 大水量：河更宽
    ("r-rnd-wa100",    20, 20, "undecided", "none", 3, 100, 0, 1, 4, 0,
     (0, 0), 0.0, 0, 1),
    # allow_waterfall=1：有机会触发瀑布
    ("r-rnd-wfall",    24, 24, "undecided", "none", 3, 100, 0, 1, 4, 1,
     (0, 0), 0.0, 0, 1),
    # level != 4：走 else Dilate(2, flag=true) 分支，不走峡谷
    ("r-rnd-lvl2",     20, 20, "undecided", "none", 3,  50, 0, 1, 2, 0,
     (0, 0), 0.0, 0, 1),
    # 中心被他人占据 → ok=false → 回滚
    ("r-rnd-disk",     20, 20, "undecided", "disk", 3,  50, 0, 2, 4, 0,
     (0, 0), 0.0, 0, 1),
    # smooth 桩返回 false → 整块回滚
    ("r-rnd-nosmooth", 20, 20, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (0, 0), 0.0, 0, 0),
    # 小地图 → steps < 40 → 失败
    ("r-rnd-small",     8,  8, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (0, 0), 0.0, 0, 1),
    # painted 非零：累加验证
    ("r-rnd-painted",  20, 20, "undecided", "none", 3,  50, 30, 1, 4, 0,
     (0, 0), 0.0, 0, 1),
    # ── start!={0,0}: 固定入口 ──
    # 向右流（angle = 0）
    ("r-fixed-right",  20, 20, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (20, 25), 0.0, 0, 1),
    # 向下流（angle = pi/2）
    ("r-fixed-down",   20, 20, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (20, 20), 1.5707963267948966, 0, 1),
    # is_branch=1：支流，不做平滑/收尾/峡谷
    ("r-branch",       20, 20, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (20, 22), 0.7853981633974483, 1, 1),
    # 固定入口坐标界外 → 立即 return false
    ("r-oob",          12, 12, "undecided", "none", 3,  50, 0, 1, 4, 0,
     (0, 1), 0.0, 0, 1),
]

# 名字, W, H, terrain, owners,
# map_type, water, painted, blob, level, allow_wf, smooth
CASES_ARL = [
    # map_type=3, water>20：先跑River（最多10次）再跑WaterBlob
    ("arl-mt3-wa30",   20, 20, "undecided", "none", 3,  30, 0, 1, 4, 0, 1),
    # map_type=4, water>20
    ("arl-mt4-wa80",   20, 20, "undecided", "none", 4,  80, 0, 1, 4, 0, 1),
    # map_type=4, allow_waterfall=1
    ("arl-mt4-wfall",  24, 24, "undecided", "none", 4,  80, 0, 1, 4, 1, 1),
    # map_type=1：跳过 River，只有 WaterBlob
    ("arl-mt1-wa80",   20, 20, "undecided", "none", 1,  80, 0, 1, 4, 0, 1),
    # map_type=3, water<=20：跳过 River
    ("arl-mt3-wa10",   20, 20, "undecided", "none", 3,  10, 0, 1, 4, 0, 1),
    # painted 非零
    ("arl-mt3-painted",20, 20, "undecided", "none", 3,  50, 30, 1, 4, 0, 1),
]

HEAD = (u"# 0x59D510 River + 0x59C580 AddRiversAndLakes 整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_river_golden.py\n"
        u"# IN/OUT 行 = x y tile subtile level owner queued allow flag4b\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493\n"
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

    for (name, w, h, terr, own,
         map_type, water, painted, blob, level, allow_wf,
         start, angle, is_branch, smooth) in CASES_RIVER:
        o = RiverOracle(w, h)
        o.smooth_ret = smooth
        o.rterrain(terr)
        o.rowners(own)
        o.seedfields(map_type, water, painted, blob, level, allow_wf)
        before = o.snap()
        ret, rng, total, blob_out, lvl_out, dummy = o.river(
            start, angle, is_branch)
        after = o.snap()
        emit(out,
             "CASE %s %d %d terrain=%s owners=%s"
             " maptype=%d water=%d painted=%d blob=%d level=%d allow_wf=%d"
             " start=%d,%d angle=%.17g is_branch=%d smooth=%d"
             " -> ret=%d rng=%d total=%d blob=%d level=%d"
             " dummy=%d,%d"
             % (name, w, h, terr, own,
                map_type, water, painted, blob, level, allow_wf,
                start[0], start[1], angle, is_branch, smooth,
                ret, rng, total, blob_out, lvl_out,
                dummy & 0xFFFF, (dummy >> 16) & 0xFFFF),
             before, after)

    for (name, w, h, terr, own,
         map_type, water, painted, blob, level, allow_wf, smooth) in CASES_ARL:
        o = RiverOracle(w, h)
        o.smooth_ret = smooth
        o.rterrain(terr)
        o.rowners(own)
        o.seedfields(map_type, water, painted, blob, level, allow_wf)
        before = o.snap()
        rng, total, blob_out, lvl_out, dummy = o.add_rivers_and_lakes()
        after = o.snap()
        emit(out,
             "CASE_ARL %s %d %d terrain=%s owners=%s"
             " maptype=%d water=%d painted=%d blob=%d level=%d allow_wf=%d"
             " smooth=%d"
             " -> rng=%d total=%d blob=%d level=%d"
             " dummy=%d,%d"
             % (name, w, h, terr, own,
                map_type, water, painted, blob, level, allow_wf,
                smooth,
                rng, total, blob_out, lvl_out,
                dummy & 0xFFFF, (dummy >> 16) & 0xFFFF),
             before, after)

    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/river_golden.txt")
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD)
        f.write(u"\n".join(out) + u"\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
