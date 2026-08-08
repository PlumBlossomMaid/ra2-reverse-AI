# -*- coding: utf-8 -*-
u"""阶段 6「Creating starting points」整段 0x594B50 的模拟执行基准。

这一份补上 dump_start_points_golden.py 缺的那一半：那边只跑最远点采样
0x594F40，这边把**调度 0x594B50 + 起区域 0x594420 + 逐区域摆点 0x594870**
连成一整条跑原版机器码。

跑真身（一个桩都不打）的有：
  0x594B50 调度 / 0x594420 GrowRegion / 0x594870 PickPointsInRegion
  0x594F40 ChooseWaypoints / 0x58CE90 清区域 / 0x58BF70 RegionClass 构造
  0x578350 + 0x578290 全图遍历器 / 0x5AC230 InDiamond / 0x5657A0 GetCellAt
  0x5A7250 AreaIsPassable / 0x578640 IsWithinUsableArea
  0x68BF50 SetWaypoint / 0x68BCC0 GetWaypoint / 0x7C8B48 qsort + 0x5AD9C0 比较器
  0x5AD740 / 0x5AC790 / 0x5AD930 / 0x58C070 / 0x42FCB0 几个向量零件

打桩的只有四处（都不是本阶段的算法，各自另有归属）：
  0x56C510 重建寻路分区  -> 直接返回「最大区编号」，真身已由 zones 基准覆盖
  0x56D230 查移动区类型  -> 按合成表返回，同上
  0x595400 PlaceRandomBuilding -> 只记一笔调用（它拉整条建筑子系统，不在本阶段）
  0x58B820 收尾同步      -> 空操作（真身要 HouseClass 数组，[0x887324] 为 0
                            时它本来也只写一句 [0xABED0C] = 0）

合成输入逐格写进基准的 IN 行（zoneid / 移动区类型 / 砖号 / 层高 / 坡道），
复刻侧照读，不用去猜生成公式。
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_PLACE_START = 0x594B50
A_ZONE_REBUILD = 0x56C510
A_ZONE_TYPE = 0x56D230
A_PLACE_BUILDING = 0x595400
A_SYNC = 0x58B820
A_RANDOM = 0x65C780

REGVEC = 0xABDF90         # DynamicVectorClass<RegionClass*>，内联在 .bss
SCENARIO = 0xA8B230       # ScenarioClass::Instance 的指针
G_ZONETAB = 0x87F800      # MoveTable 行指针数组，[0x87F800 + mz*4]
G_SLFILL = 0xABE300       # 出生点表的填充值（MapSeedClass +0x328）
SLFILL = 0x7FFF7FFF

# 合成砖号全局（温和气候里 PaveTile / MiscPaveTile 都是 -1，
# 那样 0x486650 会把砖号 0..14 全判成「铺装」，压不出拒绝分支）
TILE_GLOBALS = {
    0xABBEC8: 300,   # PavedRoads，宽 0x0F
    0xABBEC4: 340,   # PavedRoadEnds，宽 4
    0xABC2B0: 600,   # PaveTile，宽 0x10
    0xAA10A4: 700,   # MiscPaveTile，宽 0x0E
}


def sx(v):
    return v - (1 << 32) if v >> 31 else v


class StartOracle(PlaceOracle):
    def __init__(self, w, h, seed, players, map_type, scale, want_type):
        from unicorn import UC_HOOK_CODE
        self.pool = {}
        self.owned = {}
        PlaceOracle.__init__(self, w, h, seed)
        self.uc.hook_del(self.h_paintable)
        self.uc.hook_del(self.h_smooth)
        for a, v in TILE_GLOBALS.items():
            self.wr32(a, v)

        # 静态初始化器 0x58BC30 的原样（增长步长 10；留 0 的话 push 静默失败）
        self.wr32(REGVEC + 0x00, 0x7ED858)
        self.wr32(REGVEC + 0x04, 0)
        self.wr32(REGVEC + 0x08, 0)
        self.uc.mem_write(REGVEC + 0x0C, b"\x01\x00")
        self.wr32(REGVEC + 0x10, 0)
        self.wr32(REGVEC + 0x14, 10)
        self.wr32(0xABED14, 0)                    # 区域编号计数器

        # MapSeedClass 的几个标量（阶段 6 只按绝对地址读）
        self.wr32(0xABE014, map_type)             # +0x3C  MapType
        self.wr32(0xABE028, players)              # +0x50  NumPlayers
        self.wr32(0xABE030, scale)                # +0x58  waypoint 系数
        self.wr32(0xABE158, w)                    # +0x180 W
        self.wr32(0xABE15C, h)                    # +0x184 H
        self.wr32(G_SLFILL, SLFILL)

        # 可用区 MapClass +0xFC..+0x108（0x594870 会各缩 4/8）
        self.wr32(0x87F8E4, 0)
        self.wr32(0x87F8E8, 0)
        self.wr32(0x87F8EC, w)
        self.wr32(0x87F8F0, h)

        # ScenarioClass：waypoint 表 +0x632、出生点表 +0x11C0
        self.scenario = self._bump(0x2000)
        self.wr32(SCENARIO, self.scenario)
        self.wr32(0x887324, 0)                    # 让 0x58B820 真身也无事可做

        # 区类型表：[0x87F800 + mz*4] -> 一行 int16，取下标 = 最大区编号
        self.zone_row = {}
        for mz in (0, 5):
            row = self._bump(0x40)
            self.uc.mem_write(row, struct.pack("<h", want_type))
            self.wr32(G_ZONETAB + mz * 4, row)
        self.want_type = want_type
        self.mzt = {}
        self.buildings = []

        uc = self.uc
        uc.hook_add(UC_HOOK_CODE, self._h_zone_rebuild,
                    begin=A_ZONE_REBUILD, end=A_ZONE_REBUILD)
        uc.hook_add(UC_HOOK_CODE, self._h_zone_type,
                    begin=A_ZONE_TYPE, end=A_ZONE_TYPE)
        uc.hook_add(UC_HOOK_CODE, self._h_building,
                    begin=A_PLACE_BUILDING, end=A_PLACE_BUILDING)
        uc.hook_add(UC_HOOK_CODE, self._h_sync, begin=A_SYNC, end=A_SYNC)

    # ---- 带回收的分配器：GrowRegion 每次起种都要一个步长 20000 的栈向量 ----
    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        free = self.pool.get(n)
        if free:
            p = free.pop()
            uc.mem_write(p, b"\0" * n)
        else:
            p = self._bump(n if n else 1)
        self.owned[p] = n
        uc.reg_write(UC_X86_REG_EAX, p)
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        p = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        n = self.owned.pop(p, None)
        if n:
            self.pool.setdefault(n, []).append(p)
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    # ---- 四个桩 ----
    def _h_zone_rebuild(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)        # 最大区编号恒为 0
        self._ret(uc, 0)

    def _h_zone_type(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        esp = uc.reg_read(UC_X86_REG_ESP)
        coord = self.rd32(esp + 4)
        v = self.rd32(coord)
        x, y = v & 0xFFFF, (v >> 16) & 0xFFFF
        uc.reg_write(UC_X86_REG_EAX, self.mzt.get((x, y), -1) & 0xFFFFFFFF)
        self._ret(uc, 12)

    def _h_building(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX
        r = uc.reg_read(UC_X86_REG_ECX)
        self.buildings.append(sx(self.rd32(r + 0x08)))
        uc.reg_write(UC_X86_REG_EAX, 1)
        self._ret(uc, 0)

    def _h_sync(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    # ---- 合成输入 ----
    def build(self, spec):
        u"""spec(x, y) -> (zoneid, movementZoneType, tile, level, ramp)。"""
        rows = []
        for (x, y) in self._diamond():
            zid, mzt, tile, lvl, ramp = spec(x, y)
            c = self.cell(x, y)
            self.wr32(c + 0x38, tile)
            self.wr32(c + 0x4C, zid)
            self.uc.mem_write(c + 0x11B, struct.pack("b", lvl))
            self.uc.mem_write(c + 0x11C, bytes([ramp & 0xFF]))
            self.wr32(c + 0x140, 0)
            self.mzt[(x, y)] = mzt
            rows.append((x, y, zid, mzt, tile, lvl, ramp))
        # 工作数组先铺成非 -1：0x58CE90 会把它整表刷回 -1，
        # 复刻侧漏掉这一步的话第 2 步一个区域也起不出来
        for i in range(self.side * self.side):
            self.wr32(self.work + i * WORK_SIZE + 0x38, 0x5A5A5A5A)
            self.wr32(self.work + i * WORK_SIZE + 0x3C, 0x5A5A5A5A)
        return rows

    # ---- 跑 + 取终态 ----
    def run(self):
        self.rng_calls = 0
        self.buildings = []
        ret = self._call(A_PLACE_START, 0)
        return ret & 0xFF, self.rng_calls

    def regions(self):
        out = []
        data = self.rd32(REGVEC + 0x04)
        for i in range(self.rd32(REGVEC + 0x10)):
            r = self.rd32(data + i * 4)
            sc = self.rd32(r + 0x16)
            cand = []
            vec = self.rd32(r + 0x00)
            if vec:
                p = self.rd32(vec + 0x04)
                for k in range(self.rd32(vec + 0x10)):
                    v = self.rd32(p + k * 4)
                    cand.append((v & 0xFFFF, (v >> 16) & 0xFFFF))
            cells = []
            p = self.rd32(r + 0x2C)
            for k in range(self.rd32(r + 0x38)):
                v = self.rd32(p + k * 4)
                cells.append((v & 0xFFFF, (v >> 16) & 0xFFFF))
            out.append(dict(id=sx(self.rd32(r + 0x08)),
                            count=sx(self.rd32(r + 0x0C)),
                            start=sx(self.rd32(r + 0x20)),
                            seed=(sc & 0xFFFF, (sc >> 16) & 0xFFFF),
                            cand=cand, cells=cells))
        return out

    def work_state(self):
        out = []
        for (x, y) in self._diamond():
            r = self.work + (y * self.side + x) * WORK_SIZE
            out.append((x, y, sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C))))
        return out

    def waypoints(self, n):
        out = []
        for i in range(n):
            v = self.rd32(self.scenario + 0x632 + i * 4)
            out.append((v & 0xFFFF, (v >> 16) & 0xFFFF))
        return out

    def start_locs(self):
        out = []
        for i in range(8):
            v = self.rd32(self.scenario + 0x11C0 + i * 4)
            out.append(v)
        return out

    def flagged(self):
        out = []
        for (x, y) in self._diamond():
            if self.rd32(self.cell(x, y) + 0x140) & 4:
                out.append((x, y))
        return out


# ────────────────────────── 合成输入 ──────────────────────────
WANT = 3
OTHER = 7


def spec_flat(w, h):
    def f(x, y):
        return (0, WANT, 0, 0, 0)
    return f


def spec_split(w, h):
    mid = (w + h) // 2
    def f(x, y):
        return (0, WANT if x < mid else OTHER, 0, 0, 0)
    return f


def spec_stripes(w, h):
    u"""3 格宽的竖条带，隔 3 格一条：八邻域也跨不过去，每条都不足 400 格
    -> 区域全被销毁，第 3~6 步全部空转。"""
    def f(x, y):
        return (0, WANT if (x // 3) % 2 == 0 else OTHER, 0, 0, 0)
    return f


def spec_zoneholes(w, h):
    def f(x, y):
        zid = 1 if (x * 13 + y * 7) % 23 == 0 else 0
        return (zid, WANT, 0, 0, 0)
    return f


def spec_tiles(w, h):
    u"""砖号压 0x5A7250 的五条谓词。⚠️ 拒绝砖必须成片而不是逐格撒——
    0x5A7250 要 6x6 全过，逐格撒 27% 的话一个候选都过不了，
    原版会拿空向量去读 out[0]（真 UB，见 §九）。"""
    def f(x, y):
        b = (x // 9 + y // 9) % 5
        if b == 3:
            tile = 999                       # 谁都不沾 -> 拒
        elif b == 4:
            tile = 305 if (x + y) % 2 else 341   # 铺路 / 桥头，两个开关都是 0 -> 拒
        else:
            tile = (0, 0xFFFF, 605, 705)[(x * 5 + y * 3) % 4]   # 四种都能过
        return (0, WANT, tile, 0, 0)
    return f


def spec_ramps(w, h):
    u"""坡道 + 层高压 0x578640 里那条「ramp 非零就按层高挪一格」的分支。"""
    def f(x, y):
        ramp = 1 if (x + y) % 5 == 0 else 0
        lvl = ((x * 3 + y) % 7) - 3
        return (0, WANT, 0, lvl, ramp)
    return f


def spec_bands(w, h):
    u"""沿 x+y 斜线切成等宽的带，带间隔 3 条斜线（八邻域跨不过去）。
    地图够大时每条带都超过 400 格的下限 -> 多个区域同时存活，
    第 3 步的排序键、第 4 步的名额分配、第 5 步的 base 累加才有得比。"""
    smin = w + 1
    def f(x, y):
        return (0, WANT if (x + y - smin) % 20 < 17 else OTHER, 0, 0, 0)
    return f


def spec_bands2(w, h):
    u"""三条**不等宽**的带：名额按面积比例分，最小的那条会分到 0。"""
    smin = w + 1
    def f(x, y):
        d = x + y - smin
        keep = d <= 19 or (23 <= d <= 35) or d >= 39
        return (0, WANT if keep else OTHER, 0, 0, 0)
    return f


def spec_halves(w, h):
    u"""中间劈一条 3 格宽的隔离带（沿 x 竖切）。"""
    mid = (w + h) // 2
    def f(x, y):
        if abs(x - mid) <= 1:
            return (0, OTHER, 0, 0, 0)
        return (0, WANT, 0, 0, 0)
    return f


SPECS = {
    "flat": spec_flat,
    "split": spec_split,
    "stripes": spec_stripes,
    "zoneholes": spec_zoneholes,
    "tiles": spec_tiles,
    "ramps": spec_ramps,
    "bands": spec_bands,
    "bands2": spec_bands2,
    "halves": spec_halves,
}

# 名字, W, H, 种子, 玩家数, MapType, waypoint 系数, 输入
CASES = [
    ("flat-p2",      20, 20, 0x12345678, 2, 1,   0, "flat"),
    ("flat-p4-sc25", 20, 20, 0x5A5A5A5A, 4, 1,  25, "flat"),
    ("flat-p8-sc100", 24, 20, 0x0BADC0DE, 8, 2, 100, "flat"),
    ("halves-p4",    24, 20, 0x0F0F0F0F, 4, 1,  50, "halves"),
    ("split-p2",     24, 20, 0x2468ACE0, 2, 0,  10, "split"),
    ("stripes-none", 20, 20, 0x11111111, 2, 1,  25, "stripes"),
    ("zoneholes-p4", 24, 20, 0x13579BDF, 4, 1,  25, "zoneholes"),
    ("tiles-p2",     24, 20, 0x0000CAFE, 2, 1,  25, "tiles"),
    ("tiles-p4-sc60", 24, 20, 0xFACEB00C, 4, 1, 60, "tiles"),
    ("ramps-p4",     24, 20, 0x0000BEEF, 4, 1,  25, "ramps"),
    ("bands-p2",     36, 30, 0x99999999, 2, 1,  25, "bands"),
    ("bands-p4",     36, 30, 0x77777777, 4, 1,  25, "bands"),
    ("bands-p8-mt2", 36, 30, 0xAAAAAAAA, 8, 2,  40, "bands"),
    ("bands2-mt0",   36, 30, 0x88888888, 2, 0,  25, "bands2"),
    ("bands2-mt1",   36, 30, 0x88888888, 2, 1,  25, "bands2"),
    ("bands2-p3",    36, 30, 0x0BADF00D, 3, 0,  70, "bands2"),
    ("flat-mt3",     20, 20, 0xCCCCCCCC, 2, 3,  25, "flat"),
]


HEAD = (u"# 阶段 6 Creating starting points（0x594B50 整段）模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_start_points2_golden.py\n"
        u"# IN   x y zoneId movementZoneType tile level ramp\n"
        u"# REG  id cellCount startPoints seedX seedY | CELLS ... | CAND ...\n"
        u"# WORK x y owner queued（只列非 (-1,-1) 的）\n"
        u"# WP   i x y（ScenarioClass +0x632）\n"
        u"# SL   i value（ScenarioClass +0x11C0，填充值 0x7FFF7FFF）\n"
        u"# FLAG x y（CellClass +0x140 的 bit2）\n"
        u"# BLD  regionId（0x595400 PlaceRandomBuilding 的调用序列）\n"
        u"# PavedRoads=300 PavedRoadEnds=340 PaveTile=600 MiscPaveTile=700\n"
        u"# wantZoneType=3\n")


def emit(out, name, w, h, seed, players, mt, scale, spec_name):
    o = StartOracle(w, h, seed, players, mt, scale, WANT)
    rows = o.build(SPECS[spec_name](w, h))
    ret, rng = o.run()
    regs = o.regions()
    nwp = players + 4
    head = ("CASE %s w=%d h=%d seed=0x%08X players=%d mt=%d scale=%d spec=%s"
            " -> ret=%d rng=%d regions=%d nextid=%d"
            % (name, w, h, seed, players, mt, scale, spec_name, ret, rng,
               len(regs), sx(o.rd32(0xABED14))))
    out.append(head)
    for (x, y, zid, mzt, tile, lvl, ramp) in rows:
        out.append("  IN %d %d %d %d %d %d %d" % (x, y, zid, mzt, tile, lvl, ramp))
    for r in regs:
        out.append("  REG %d %d %d %d %d %d %d"
                   % (r["id"], r["count"], r["start"], r["seed"][0],
                      r["seed"][1], len(r["cells"]), len(r["cand"])))
        out.append("  CAND " + (",".join("%d:%d" % p for p in r["cand"])
                                if r["cand"] else "-"))
        out.append("  CELLS " + (",".join("%d:%d" % p for p in r["cells"])
                                 if r["cells"] else "-"))
    for (x, y, ow, q) in o.work_state():
        if ow != -1 or q != -1:
            out.append("  WORK %d %d %d %d" % (x, y, ow, q))
    for i, (x, y) in enumerate(o.waypoints(nwp)):
        out.append("  WP %d %d %d" % (i, x, y))
    for i, v in enumerate(o.start_locs()):
        out.append("  SL %d %d" % (i, v))
    for (x, y) in o.flagged():
        out.append("  FLAG %d %d" % (x, y))
    out.append("  BLD " + (",".join(str(i) for i in o.buildings)
                           if o.buildings else "-"))
    print(head)
    sys.stdout.flush()


def main():
    out = []
    for (name, w, h, seed, players, mt, scale, spec) in CASES:
        emit(out, name, w, h, seed, players, mt, scale, spec)
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/start_points2_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
