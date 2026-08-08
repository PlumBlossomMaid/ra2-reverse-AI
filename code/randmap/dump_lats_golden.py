# -*- coding: utf-8 -*-
u"""阶段 10「Creating LATs, rocks etc」的整函数模拟执行。

五个入口全部跑原版机器码：
  0x5A38C0 播概率图（__fastcall ecx = MapSeedClass）
  0x5A3AE0 温带主体（同上）
  0x5A4280 雪地主体（同上）
  0x5A45E0 树林补丁生长（cell, size, prob）
  0x5A4B60 LAT 块写入器（cell, tile, count, fieldId, allowNonPlain）

不打桩的：PickRange 0x598030、NormalDeviate 0x5980C0、FastSqrt 0x4CAC40、
GetCellAt 0x5657A0、CellStruct 加/比较 0x42D510/0x50E470/0x42D470、
IsPlain 0x486380、IsShorePiece 0x4865B0、IsClearToSandLat 0x486790、
IsGreen 0x4867B0、0x42B1F0 地图规模、堆的上浮/下沉/弹出。

打桩的只有「引擎那一侧」：
  0x47CA80 CellClass::SetupLAT     —— 只记调用次数（它要读 .tmp 美术资源）
  0x47D2B0 CellClass::RecalcAttributes —— 只记 (格子, 参数)
  0x7C8EF4 sprintf / 0x71DD80 FindIndex / 0x71BB90 TerrainClass::TerrainClass
                                    —— 记下 "TREExx" 的名字与落点
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_SEEDVEG = 0x5A38C0
A_TEMPERATE = 0x5A3AE0
A_SNOW = 0x5A4280
A_TREE = 0x5A45E0
A_WRITE = 0x5A4B60
A_SETUPLAT = 0x47CA80
A_RECALC = 0x47D2B0
A_FINDIDX = 0x71DD80
A_TERRAIN = 0x71BB90
A_SPRINTF = 0x7C8EF4
DUMMY = 0xABDC50
MAP = 0x87F7E8

# temperate 的另外几个砖号全局（tileset_starts.txt），PlaceOracle 没铺的那批
LAT_THEATER = {
    0xAA0E18: 493,   # GreenTile
    0xAA0748: 494,   # ClearToGreenLat（16 块）
    0xAA10B0: 0,     # ClearTile
    0xABC2B8: 131,   # RoughTile
    0xABB104: 418,   # SandTile
    0xAA0E24: 419,   # ClearToSandLat（16 块）
    0xABC2CC: 1,     # BlackTile
    # 下面两个 temperate 里其实是 -1（未定义）。这里给两个**合成值**，
    # 只为把 0x5A4B60 的 allowNonPlain 死分支（0x486670 / 0x486650）走一遍。
    0xABC2B0: 600,   # PaveTile（宽 0x10）
    0xAA10A4: 700,   # MiscPaveTile（宽 0x0E）
}


class LatOracle(PlaceOracle):
    def __init__(self, w, h, seed=1, veg=50, width=64, max_trees=500):
        PlaceOracle.__init__(self, w, h, seed)
        for a, v in LAT_THEATER.items():
            self.wr32(a, v)
        # 每格：砖号 0（未定砖）、overlay -1、其余归零
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            self.wr32(c + 0x38, 0)
            self.wr32(c + 0x44, 0xFFFFFFFF)
            self.wr32(c + 0xE4, 0)
            self.wr32(c + 0xEC, 0)
            self.uc.mem_write(c + 0x11C, b"\x00")
            self.uc.mem_write(c + 0x11E, b"\x00")
        for off in range(0, CELL_SIZE, 4):
            self.wr32(DUMMY + off, 0)
        self.wr32(DUMMY + 0x44, 0xFFFFFFFF)
        # MapSeedClass：只读 +0x38 / +0x5C / +0x64 / +0x2FC 四个标量
        self.seedobj = self._bump(0x400)
        self.wr32(self.seedobj + 0x5C, veg)
        self.wr32(self.seedobj + 0x64, width)
        self.wr32(self.seedobj + 0x2FC, max_trees)
        # TerrainTypes [0xA8E31C]
        tt = self._bump(64)
        self.wr32(tt, self._bump(16))
        self.wr32(0xA8E31C, tt)
        self.terrains = []
        self.setuplat = 0
        self.recalc = []
        self._pending = ""
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE, self._h_setuplat, begin=A_SETUPLAT, end=A_SETUPLAT)
        self.uc.hook_add(UC_HOOK_CODE, self._h_recalc, begin=A_RECALC, end=A_RECALC)
        self.uc.hook_add(UC_HOOK_CODE, self._h_sprintf, begin=A_SPRINTF, end=A_SPRINTF)
        self.uc.hook_add(UC_HOOK_CODE, self._h_findidx, begin=A_FINDIDX, end=A_FINDIDX)
        self.uc.hook_add(UC_HOOK_CODE, self._h_terrain, begin=A_TERRAIN, end=A_TERRAIN)

    # ---- 五个桩 ----
    def _h_setuplat(self, uc, address, size, ud):
        self.setuplat += 1
        self._ret(uc, 0)

    def _h_recalc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
        cellp = uc.reg_read(UC_X86_REG_ECX)
        arg = struct.unpack("<i", self.uc.mem_read(
            uc.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
        xy = self.rd32(cellp + 0x24)
        self.recalc.append((xy & 0xFFFF, (xy >> 16) & 0xFFFF, arg))
        self._ret(uc, 4)

    def _h_sprintf(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        esp = uc.reg_read(UC_X86_REG_ESP)
        buf = self.rd32(esp + 4)
        a1 = self.rd32(esp + 0xC)
        a2 = self.rd32(esp + 0x10)
        s = ("TREE%d%d" % (a1, a2)).encode("ascii")
        self.uc.mem_write(buf, s + b"\0")
        uc.reg_write(UC_X86_REG_EAX, len(s))
        self._ret(uc, 0)

    def _h_findidx(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX
        raw = bytes(self.uc.mem_read(uc.reg_read(UC_X86_REG_ECX), 16))
        self._pending = raw.split(b"\0")[0].decode("ascii")
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    def _h_terrain(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX
        esp = uc.reg_read(UC_X86_REG_ESP)
        at = self.rd32(self.rd32(esp + 8))
        self.terrains.append((self._pending, at & 0xFFFF, (at >> 16) & 0xFFFF))
        uc.reg_write(UC_X86_REG_EAX, uc.reg_read(UC_X86_REG_ECX))
        self._ret(uc, 8)

    # ---- 环境布置 ----
    def fill_work_coords(self):
        u"""阶段 1 只给菱形内的格子填打包坐标；菱形外的槽位恒为 (0,0)。"""
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE,
                      (y << 16) | x)

    def set_cellv(self, x, y, tile=None, ramp=None, overlay=None, e4=None,
                  ec=None):
        c = self.cell(x, y)
        if tile is not None:
            self.wr32(c + 0x38, tile)
        if ramp is not None:
            self.uc.mem_write(c + 0x11C, bytes([ramp & 0xFF]))
        if overlay is not None:
            self.wr32(c + 0x44, overlay & 0xFFFFFFFF)
        if e4 is not None:
            self.wr32(c + 0xE4, e4)
        if ec is not None:
            self.wr32(c + 0xEC, ec)

    def set_work(self, x, y, queued=None, flag45=None, flag47=None,
                 rough=None, green=None, sand=None):
        b = self.work + (y * self.side + x) * WORK_SIZE
        if queued is not None:
            self.wr32(b + 0x3C, queued & 0xFFFFFFFF)
        if flag45 is not None:
            self.uc.mem_write(b + 0x45, bytes([1 if flag45 else 0]))
        if flag47 is not None:
            self.uc.mem_write(b + 0x47, bytes([1 if flag47 else 0]))
        for off, v in ((0x18, rough), (0x20, green), (0x28, sand)):
            if v is not None:
                self.uc.mem_write(b + off, struct.pack("<d", v))

    # ---- 观测 ----
    def snapshot(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            b = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (
                self.rd32(c + 0x38),
                struct.unpack("<i", self.uc.mem_read(c + 0x44, 4))[0],
                self.uc.mem_read(c + 0x11E, 1)[0],
                struct.unpack("<i", self.uc.mem_read(b + 0x3C, 4))[0],
                self.uc.mem_read(b + 0x47, 1)[0],
                struct.unpack("<d", self.uc.mem_read(b + 0x18, 8))[0],
                struct.unpack("<d", self.uc.mem_read(b + 0x20, 8))[0],
                struct.unpack("<d", self.uc.mem_read(b + 0x28, 8))[0])
        return out

    def _reset_counters(self):
        self.rng_calls = 0
        self.terrains = []
        self.setuplat = 0
        self.recalc = []

    # ---- 五个入口 ----
    def run_write(self, seed_xy, tile, count, field_id, allow):
        before = self.snapshot()
        self._reset_counters()
        cellp = self.cell(seed_xy[0], seed_xy[1])
        self._call(A_WRITE, 0, (cellp, tile, count, field_id, allow))
        after = self.snapshot()
        return before, after

    def run_tree(self, seed_xy, size, prob):
        before = self.snapshot()
        self._reset_counters()
        cellp = self.cell(seed_xy[0], seed_xy[1])
        lo, hi = struct.unpack("<II", struct.pack("<d", prob))
        from unicorn.x86_const import UC_X86_REG_EAX
        self._call(A_TREE, 0, (cellp, size, lo, hi))
        return before, self.snapshot(), self.uc.reg_read(UC_X86_REG_EAX)

    def run_fn(self, addr):
        before = self.snapshot()
        self._reset_counters()
        self._call(addr, self.seedobj, ())
        return before, self.snapshot()

    def run_dispatch(self, theater):
        u"""0x59921F 起那一小段调度：读 +0x38 分流，雪地先内联铺两个常量。

        直接跑原版机器码（不是在 Python 里复述），这样「雪地那圈遍历到底
        覆盖哪些格子」也是实测出来的。"""
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EBX,
                                       UC_X86_REG_EDI)
        self.wr32(self.seedobj + 0x38, theater)
        before = self.snapshot()
        self._reset_counters()
        self.uc.reg_write(UC_X86_REG_ESP, 0x20000000 + 0x100000 - 0x2000)
        self.uc.reg_write(UC_X86_REG_EBX, 0)
        self.uc.reg_write(UC_X86_REG_EDI, self.seedobj)
        self.uc.emu_start(0x59921F, 0x5992B0)
        return before, self.snapshot()


# ---------------------------------------------------------------- 用例表
# 每格的缺省态：砖 0（未定）、ramp 0、overlay -1、+0xE4/+0xEC 0、
# 工作记录 +0x3C/+0x45/+0x47 与三个概率槽全 0。
DEFAULT = (0, 0, -1, 0, 0, 0, 0, 0, 0.0, 0.0, 0.0)
KEYS = ("tile", "ramp", "overlay", "e4", "ec",
        "queued", "flag45", "flag47", "rough", "green", "sand")


def diamond(w, h):
    return [(x, y) for y in range(0, w + 2 * h + 2)
            for x in range(0, w + 2 * h + 2)
            if x + y > w and x - y < w and y - x < w and x + y <= w + 2 * h]


CASES = []


def case(name, kind, w, h, seed=1, veg=50, width=12, max_trees=100,
         args=(), cells=()):
    CASES.append(dict(name=name, kind=kind, w=w, h=h, seed=seed, veg=veg,
                      width=width, max_trees=max_trees, args=tuple(args),
                      cells=list(cells)))


# ---- 0x5A4B60 LAT 块写入器：args = (sx, sy, tile, count, fieldId, allow)
case("w-plain", "write", 12, 8, args=(10, 10, 131, 12, 7, 0))
case("w-count1", "write", 12, 8, args=(10, 10, 131, 1, 7, 0))
case("w-big", "write", 20, 14, seed=7, args=(16, 16, 418, 40, 3, 0))
# 一半格子不是「未定砖」——扩散被切碎
case("w-blocked", "write", 12, 8, seed=3, args=(10, 10, 131, 12, 7, 0),
     cells=[(x, y, {"tile": 494}) for (x, y) in diamond(12, 8)
            if (x * 5 + y * 3) % 4 == 0])
# 三种「硬拦」：ramp / overlay / +0xE4
case("w-obstacles", "write", 12, 8, seed=5, args=(10, 10, 131, 16, 7, 0),
     cells=([(x, y, {"ramp": 3}) for (x, y) in diamond(12, 8) if x == 9]
            + [(x, y, {"overlay": 5}) for (x, y) in diamond(12, 8) if x == 12]
            + [(x, y, {"e4": 1}) for (x, y) in diamond(12, 8) if y == 8]))
# 已经属于同一田块（+0x3C == fieldId）的格子不再入堆
case("w-queued", "write", 12, 8, seed=9, args=(10, 10, 131, 16, 7, 0),
     cells=[(x, y, {"queued": 7}) for (x, y) in diamond(12, 8) if y % 3 == 0])
# 死分支：allow=1 + 铺装砖（PaveTile 600..615 / MiscPaveTile 700..713）
case("w-pave", "write", 12, 8, seed=11, args=(10, 10, 131, 16, 7, 1),
     cells=([(x, y, {"tile": 604}) for (x, y) in diamond(12, 8) if (x + y) % 3 == 0]
            + [(x, y, {"tile": 705}) for (x, y) in diamond(12, 8) if (x + y) % 3 == 1]))

# ---- 0x5A45E0 树林补丁：args = (sx, sy, size, prob)
case("t-plain", "tree", 12, 8, args=(10, 10, 20, 0.5))
case("t-low", "tree", 12, 8, seed=2, args=(10, 10, 20, 0.03))
case("t-tiny", "tree", 12, 8, seed=4, args=(10, 10, 2, 0.9))
case("t-big", "tree", 20, 14, seed=6, args=(16, 16, 30, 0.35))
# +0x45 锁定 / +0x47 已走过：两条不同的拦截
case("t-flag45", "tree", 12, 8, seed=8, args=(10, 10, 20, 0.5),
     cells=[(x, y, {"flag45": 1}) for (x, y) in diamond(12, 8) if x % 2 == 0])
case("t-flag47", "tree", 12, 8, seed=10, args=(10, 10, 20, 0.5),
     cells=[(x, y, {"flag47": 1}) for (x, y) in diamond(12, 8) if y % 2 == 0])
# 栽树自身的四条门槛：非未定砖 / +0xE4 / overlay / +0xEC == 3
case("t-blocked", "tree", 12, 8, seed=12, args=(10, 10, 20, 0.8),
     cells=([(x, y, {"tile": 494}) for (x, y) in diamond(12, 8) if (x * 3 + y) % 5 == 0]
            + [(x, y, {"e4": 1}) for (x, y) in diamond(12, 8) if (x * 3 + y) % 5 == 1]
            + [(x, y, {"overlay": 2}) for (x, y) in diamond(12, 8) if (x * 3 + y) % 5 == 2]
            + [(x, y, {"ec": 3}) for (x, y) in diamond(12, 8) if (x * 3 + y) % 5 == 3]))

# ---- 0x5A38C0 播概率图（无参）
case("v-plain", "veg", 12, 8)
case("v-shore", "veg", 12, 8, veg=80,
     cells=[(x, y, {"tile": 89 + (x + y) % 42}) for (x, y) in diamond(12, 8)
            if (x + y) % 5 == 0])
case("v-veg0", "veg", 12, 8, veg=0)
# +0x28 非零的格子不再播种（「已被别的阶段占过」那条）
case("v-taken", "veg", 12, 8, veg=30,
     cells=[(x, y, {"sand": 0.25}) for (x, y) in diamond(12, 8) if y % 2 == 0])

# ---- 0x59921F 调度 + 两条主体
case("temp-a", "temp", 12, 8, seed=1)
case("temp-b", "temp", 12, 8, seed=1234, veg=80, width=12, max_trees=200)
case("temp-veg0", "temp", 12, 8, seed=77, veg=0)
case("snow-a", "snow", 12, 8, seed=1)
case("snow-b", "snow", 12, 8, seed=4321, veg=80, max_trees=200)
# 大一号的地图：撒石头那段的名额 count = PickBelowMul(n/200+1) 才有得挑
case("temp-big", "temp", 24, 16, seed=99, veg=60, width=24, max_trees=300)
case("snow-big", "snow", 24, 16, seed=99, veg=60, width=24, max_trees=300)


def build(c):
    o = LatOracle(c["w"], c["h"], seed=c["seed"], veg=c["veg"],
                  width=c["width"], max_trees=c["max_trees"])
    o.fill_work_coords()
    for (x, y, kw) in c["cells"]:
        o.set_cellv(x, y, tile=kw.get("tile"), ramp=kw.get("ramp"),
                    overlay=kw.get("overlay"), e4=kw.get("e4"),
                    ec=kw.get("ec"))
        o.set_work(x, y, queued=kw.get("queued"), flag45=kw.get("flag45"),
                   flag47=kw.get("flag47"), rough=kw.get("rough"),
                   green=kw.get("green"), sand=kw.get("sand"))
    return o


def num(v):
    return "%.17g" % v


def emit(w, c):
    o = build(c)
    a = c["args"]
    ret = -1
    if c["kind"] == "write":
        before, after = o.run_write((a[0], a[1]), a[2], a[3], a[4], a[5])
        argstr = " ".join(str(v) for v in a)
    elif c["kind"] == "tree":
        before, after, ret = o.run_tree((a[0], a[1]), a[2], a[3])
        argstr = "%d %d %d %s" % (a[0], a[1], a[2], num(a[3]))
    elif c["kind"] == "veg":
        before, after = o.run_fn(A_SEEDVEG)
        argstr = "-"
    else:
        before, after = o.run_dispatch(0 if c["kind"] == "temp" else 1)
        argstr = "-"

    w(u"CASE %s %s %d %d %d %d %d %d %s\n" % (
        c["name"], c["kind"], c["w"], c["h"], c["seed"], c["veg"],
        c["width"], c["max_trees"], argstr))
    for (x, y, kw) in c["cells"]:
        vals = []
        for i, k in enumerate(KEYS):
            v = kw.get(k, DEFAULT[i])
            vals.append(num(v) if isinstance(v, float) else str(v))
        w(u"IN %d %d %s\n" % (x, y, " ".join(vals)))
    w(u"RES %d %d %d %d %d\n" % (o.rng_calls, ret, o.setuplat,
                                 len(o.terrains), len(o.recalc)))
    for k in sorted(after):
        if after[k] != before[k]:
            t, ov, od, q, f47, ro, gr, sa = after[k]
            w(u"OUT %d %d %d %d %d %d %d %s %s %s\n" % (
                k[0], k[1], t, ov, od, q, f47, num(ro), num(gr), num(sa)))
    for (nm, x, y) in o.terrains:
        w(u"TERR %s %d %d\n" % (nm, x, y))
    for (x, y, arg) in o.recalc:
        w(u"RECALC %d %d %d\n" % (x, y, arg))
    w(u"END\n")


def main():
    import io
    import os
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "memory", "data", "rmg", "lats_golden.txt")
    buf = io.StringIO()
    w = buf.write
    w(u"# 阶段 10「Creating LATs, rocks etc」对拍基准\n")
    w(u"# 生成脚本 code/randmap/dump_lats_golden.py（Unicorn 跑原版机器码）\n")
    w(u"# CASE name kind w h seed veg width maxtrees [args]\n")
    w(u"# IN   x y tile ramp overlay e4 ec queued flag45 flag47 rough green sand\n")
    w(u"# RES  rng ret setuplat nterr nrecalc\n")
    w(u"# OUT  x y tile overlay overlaydata queued flag47 rough green sand（只列变化的格）\n")
    w(u"# TERR name x y     RECALC x y arg\n")
    for c in CASES:
        sys.stderr.write("  %s\n" % c["name"])
        emit(w, c)
    with io.open(os.path.normpath(out), "w", encoding="utf-8", newline="\n") as f:
        f.write(buf.getvalue())
    sys.stderr.write("wrote %s\n" % os.path.normpath(out))


if __name__ == "__main__":
    main()
