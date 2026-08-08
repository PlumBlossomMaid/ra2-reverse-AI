# -*- coding: utf-8 -*-
u"""阶段 8 外层 0x5A23A0 PlaceTiberium 的整函数模拟执行基准。

内层 0x5A28C0 CreateOreField 已经由 dump_tiberium_golden.py 单独对过；
这一份把**外壳**也接上跑：真值表、参考点、最近候选点、矿量公式、
正态抖动窗口、出生点补偿 (avg-mn)*15+500，以及那条「候选向量指针为空
就整段跳过、连 wpBase 都不推进」的分支。

跑真身（一个桩都不打）的有：
  0x5A23A0 PlaceTiberium / 0x5A28C0 CreateOreField
  0x5980C0 NormalDeviate（+ 0x598000 UniformUnit）/ 0x4CAC40 FastSqrt
  0x68BCC0 ScenarioClass::GetWaypointCoords / 0x5ADAB0 VectorClass<double> 构造
  0x5AD0F0 等向量虚函数 / 0x7C5F00 _ftol / 0x578460 IsWithinUsableArea
  0x5657A0 GetCellAt / 0x486380 未定砖 / 0x5AD870 堆下沉

打桩的仍是 dump_tiberium_golden.py 那四处引擎依赖：
  0x485010 GetContainedTiberiumIndex / 0x7C8EF4 sprintf
  0x71DD80 FindIndex / 0x71BB90 TerrainClass::TerrainClass

RegionClass 与 ScenarioClass 的 waypoint 表都是**手搭**的：本函数只读
RegionClass 的 +0x00（候选向量指针）、+0x20（出生点数）、+0x2C/+0x38
（格子表），别的一概不碰。
"""
import io
import struct
import sys

from dump_tiberium_golden import OreOracle
from dump_blob_paint_golden import WORK_SIZE

A_PLACE_TIB = 0x5A23A0
REGVEC = 0xABDF90         # DynamicVectorClass<RegionClass*>，内联在 .bss
SCENARIO = 0xA8B230       # ScenarioClass::Instance 的指针
NWAYPOINT = 32            # 基准里预置的 waypoint 条数


def sx(v):
    return v - (1 << 32) if v >> 31 else v


class TibOracle(OreOracle):
    def __init__(self, w, h, seed):
        OreOracle.__init__(self, w, h, seed)
        # 静态初始化器 0x58BC30 的原样（增长步长 10）
        self.wr32(REGVEC + 0x00, 0x7ED858)
        self.wr32(REGVEC + 0x04, 0)
        self.wr32(REGVEC + 0x08, 0)
        self.uc.mem_write(REGVEC + 0x0C, b"\x01\x00")
        self.wr32(REGVEC + 0x10, 0)
        self.wr32(REGVEC + 0x14, 10)

        # ScenarioClass：只用到 waypoint 表 +0x632
        self.scenario = self._bump(0x2000)
        self.wr32(SCENARIO, self.scenario)

        # MapSeedClass：本阶段只读 +0x3C/+0x40/+0x54/+0x2BC/+0x2C0
        self.seedobj = self._bump(0x400)

        self.regvec_data = self._bump(4 * 64)
        self.wr32(REGVEC + 0x04, self.regvec_data)
        self.wr32(REGVEC + 0x08, 64)
        self.nregion = 0
        self.allocs = []

    # 每次 malloc 记一笔，好让池子写入点能判越界
    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        p = self._bump(n if n else 1)
        self.allocs.append((p, n))
        uc.reg_write(UC_X86_REG_EAX, p)
        self._ret(uc, 0)

    def _h_poolwr(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EDI
        p = uc.reg_read(UC_X86_REG_EDI)
        for lo, n in self.allocs:
            if lo <= p < lo + n:
                return
        raise AssertionError("池子写越界：0x%08X 不在任何分配块内" % p)

    # ---- 搭环境 ----
    def set_params(self, map_type, resources, tib, mn, mx):
        self.wr32(self.seedobj + 0x03C, map_type)
        self.wr32(self.seedobj + 0x040, resources)
        self.wr32(self.seedobj + 0x054, tib)
        self.wr32(self.seedobj + 0x2BC, mn)
        self.wr32(self.seedobj + 0x2C0, mx)

    def set_waypoints(self, pts):
        for i, (x, y) in enumerate(pts):
            self.wr32(self.scenario + 0x632 + i * 4, (y << 16) | x)

    def add_region(self, start_points, cand, cells):
        u"""cand 为 None 表示 +0x00 是**空指针**（原版构造后的初值）。"""
        r = self._bump(0x50)
        if cand is None:
            self.wr32(r + 0x00, 0)
        else:
            vec = self._bump(0x18)
            data = self._bump(4 * max(len(cand), 1))
            for i, (x, y) in enumerate(cand):
                self.wr32(data + i * 4, (y << 16) | x)
            self.wr32(vec + 0x00, 0x7ED858)
            self.wr32(vec + 0x04, data)
            self.wr32(vec + 0x08, max(len(cand), 1))
            self.wr32(vec + 0x10, len(cand))
            self.wr32(vec + 0x14, 10)
            self.wr32(r + 0x00, vec)
        self.wr32(r + 0x08, self.nregion)
        self.wr32(r + 0x20, start_points)
        cdata = self._bump(4 * max(len(cells), 1))
        for i, (x, y) in enumerate(cells):
            self.wr32(cdata + i * 4, (y << 16) | x)
        self.wr32(r + 0x2C, cdata)
        self.wr32(r + 0x38, len(cells))
        self.wr32(self.regvec_data + self.nregion * 4, r)
        self.nregion += 1
        self.wr32(REGVEC + 0x10, self.nregion)
        return r

    # ---- 跑 ----
    def run(self):
        before = self.snapshot()
        self.rng_calls = 0
        self.terrains = []
        self._call(A_PLACE_TIB, self.seedobj)
        after = self.snapshot()
        diff = sorted((k, after[k]) for k in after if after[k] != before[k])
        return diff, self.rng_calls, list(self.terrains)


# ────────────────────────── 用例 ──────────────────────────
W, H = 12, 8


def split_cells(o):
    u"""按 x 把菱形劈成两半，当成两个区域的格子表。"""
    cells = o._diamond()
    xs = sorted(set(x for (x, y) in cells))
    mid = xs[len(xs) // 2]
    left = [c for c in cells if c[0] < mid]
    right = [c for c in cells if c[0] >= mid]
    return left, right


def pick(cells, n, step):
    return [cells[(i * step + step // 2) % len(cells)] for i in range(n)]


def spec_two(o):
    left, right = split_cells(o)
    return [(2, pick(left, 4, 17), left),
            (1, pick(right, 3, 23), right)]


def spec_nullcand(o):
    u"""第一个区域的 +0x00 是空指针：整段跳过，**wpBase 也不推进**——
    于是第二个区域拿到的还是 0 号 waypoint。"""
    left, right = split_cells(o)
    return [(2, None, left),
            (1, pick(right, 3, 23), right)]


def spec_emptycand(o):
    u"""指针非空但元素为 0：先把参考点那一段跑完（不花随机数，因为 sp>0），
    再在 0x5A25A7 处跳掉——同样不推进 wpBase。"""
    left, right = split_cells(o)
    return [(2, [], left),
            (1, pick(right, 3, 23), right)]


def spec_nullcand0(o):
    u"""空指针 + 0 个出生点：一个随机数都不花。与下面的 emptycand0 成对——
    两者只差 +0x00 是不是空指针，而观测到的随机数个数不同，正好证明
    0x5A23F1 判的是指针而不是元素个数。"""
    left, right = split_cells(o)
    return [(0, None, left),
            (2, pick(right, 3, 23), right)]


def spec_emptycand0(o):
    u"""指针非空、元素为 0、0 个出生点：先在 0x5A2494 抽一次参考点
    （PickBelowMul，拒绝采样可能抽多次），再在 0x5A25A7 跳掉。"""
    left, right = split_cells(o)
    return [(0, [], left),
            (2, pick(right, 3, 23), right)]


def spec_sp0(o):
    u"""第一个区域 0 个出生点：参考点走 0x5A2494 的 PickBelowMul 那一路
    （花随机数），矿量乘子按 0.5 算。"""
    left, right = split_cells(o)
    return [(0, pick(left, 4, 17), left),
            (2, pick(right, 3, 23), right)]


def spec_three(o):
    u"""三个区域，中间那个空指针：检验 wpBase 的累加顺序。"""
    cells = o._diamond()
    n = len(cells)
    a = cells[:n // 3]
    b = cells[n // 3:2 * n // 3]
    c = cells[2 * n // 3:]
    return [(1, pick(a, 2, 11), a),
            (2, None, b),
            (2, pick(c, 3, 13), c)]


# name, mapType, resources, tiberium, min, max, spec, rngSeed
CASES = [
    ("mt1-res1",  1, 1, 50,  20,  60, spec_two,       1),
    ("mt0-res1",  0, 1, 50,  20,  60, spec_two,       2),
    ("mt0-res3",  0, 3, 50,  20,  60, spec_two,       3),
    ("mt2-res0",  2, 0, 50,  20,  60, spec_two,       4),
    ("mt3-res3",  3, 3, 50,  20,  60, spec_two,       5),
    ("mt1-res3",  1, 3, 50,  20,  60, spec_two,       6),
    ("mt9-res1",  9, 1, 50,  20,  60, spec_two,       7),
    ("nullcand",  1, 1, 50,  20,  60, spec_nullcand,  8),
    ("emptycand", 1, 1, 50,  20,  60, spec_emptycand, 8),
    ("sp0",       1, 1, 50,  20,  60, spec_sp0,      10),
    ("nullcand0", 1, 1, 50,  20,  60, spec_nullcand0, 8),
    ("emptycand0",1, 1, 50,  20,  60, spec_emptycand0,8),
    ("three",     2, 1, 50,  20,  60, spec_three,    11),
    ("zerotib",   1, 1,  0,   0,   0, spec_two,      12),
    ("tinytib",   1, 1, 50,   1,   1, spec_two,      13),
    ("bigtib",    1, 1, 90, 100, 200, spec_two,      14),
]


def fmt_pts(pts):
    return ",".join("%d:%d" % p for p in pts)


def main():
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/tiberium2_golden.txt")
    out = io.open(path, "w", encoding="utf-8", newline="\n")
    out.write(u"# 阶段 8 PlaceTiberium（0x5A23A0 整段）模拟执行基准\n")
    out.write(u"# 生成脚本 code/randmap/dump_tiberium2_golden.py\n")
    out.write(u"# CASE name w h mt res tib min max rngseed -> rng terr changed\n")
    out.write(u"# WP    i x y            （ScenarioClass +0x632 预置表）\n")
    out.write(u"# REG   i sp | CAND ... | CELLS ...  "
              u"（CAND 为 - 表示 +0x00 是空指针，. 表示空向量）\n")
    out.write(u"# TERR  name x y         （TIBTRE 地貌物的建造序列）\n")
    out.write(u"# OUT   x y overlay data queued      （跑完发生变化的格）\n")
    for name, mt, res, tib, mn, mx, spec, rseed in CASES:
        o = TibOracle(W, H, rseed)
        o.set_params(mt, res, tib, mn, mx)
        cells = o._diamond()
        wps = [cells[(i * 7 + 3) % len(cells)] for i in range(NWAYPOINT)]
        o.set_waypoints(wps)
        regs = spec(o)
        for sp, cand, rc in regs:
            o.add_region(sp, cand, rc)
        diff, rng, terr = o.run()
        out.write("CASE %s %d %d %d %d %d %d %d %d -> %d %d %d\n" % (
            name, W, H, mt, res, tib, mn, mx, rseed, rng, len(terr), len(diff)))
        for i, (x, y) in enumerate(wps):
            out.write("  WP %d %d %d\n" % (i, x, y))
        for i, (sp, cand, rc) in enumerate(regs):
            tag = "-" if cand is None else ("." if not cand else fmt_pts(cand))
            out.write("  REG %d %d\n" % (i, sp))
            out.write("  CAND %s\n" % tag)
            out.write("  CELLS %s\n" % fmt_pts(rc))
        for nm, tx, ty in terr:
            out.write("  TERR %s %d %d\n" % (nm, tx, ty))
        for (x, y), (ov, data, q) in diff:
            out.write("  OUT %d %d %d %d %d\n" % (x, y, ov, data, q))
        sys.stderr.write("%-10s rng=%-5d terr=%-3d changed=%d\n" % (
            name, rng, len(terr), len(diff)))
    out.close()
    sys.stderr.write("wrote %s\n" % path)


if __name__ == "__main__":
    main()
