# -*- coding: utf-8 -*-
u"""阶段 7 分支 B（0x5A95B0 的 MapType != 2 那半边）的整函数模拟执行。

跑的是真身：摸格子的 PickRange（0x598030）、CellStruct 的构造与相等
（0x42D470 / 0x50E470）、GetCellAt（0x5657A0）、IsPlain（0x486380）、
IsWithinUsableArea（0x578540）全都原样执行。只有「建筑对象」这一侧换成桩，
因为 BuildingClass 是引擎的东西，与本阶段的算法无关：

  0x5117D0 FindIndexByName("Neutral") -> 0
  0x502D30 FindHouseByType            -> 一个假的 HouseClass 指针
  0x7C8E17 operator new(0x720)        -> bump 分配（可以配成返回 0）
  0x43B740 BuildingClass::BuildingClass -> 就地拼一个假对象：
             [obj+0x000] = 假虚表 A（+0x20 销毁、+0xD8 落地）
             [obj+0x520] = 假类型   -> [type] = 假虚表 B（+0x90 占地表）
  虚函数 +0x90  -> 返回一张写死的占地表（short 对，(0x7FFF,0x7FFF) 收尾）
  虚函数 +0xD8  -> 记下落点；成功时把占地格的 +0xE4 写成对象指针（模拟
                   Unlimbo 会占住格子），返回值由用例指定
  虚函数 +0x20  -> 只记一笔「被销毁」

C++ 侧 test_tech_buildings.cpp 的 FakeTechWorld 用**同一套桩语义**，
所以这份基准检验的是算法本身：随机数个数与顺序、100 次尝试 / 200 次摸格的
两层预算、占地检查的六条与门、以及最后哪些格子被占住。

⚠️ 分支 A（MapType == 2）依赖 0x595400（阶段 6 那个 PlaceRandomBuilding），
它自己还要摸一整套区域对象，这里不收——C++ 侧把它做成虚函数交给调用方。
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE, SCRATCH, SCRATCH_SIZE

A_TECH = 0x5A95B0
A_FINDHOUSETYPE = 0x5117D0
A_FINDHOUSE = 0x502D30
A_NEW = 0x7C8E17
A_BLDCTOR = 0x43B740
A_PLACERND = 0x595400    # 阶段 6 的 PlaceRandomBuilding，分支 A 才走
MAP = 0x87F7E8
RULES = 0x8871E0

# 三个假虚函数的落点：SCRATCH 尾部，bump 分配器永远走不到那儿
V_FOUND = SCRATCH + SCRATCH_SIZE - 0x100
V_UNLIMBO = SCRATCH + SCRATCH_SIZE - 0x200
V_DESTROY = SCRATCH + SCRATCH_SIZE - 0x300


class TechOracle(PlaceOracle):
    def __init__(self, w, h, seed=1, foundation=((0, 0),), unlimbo=True,
                 new_fails=False, type_count=4):
        PlaceOracle.__init__(self, w, h, seed)
        from unicorn import UC_HOOK_CODE
        self.unlimbo_ok = unlimbo
        self.new_fails = new_fails
        self.placements = []      # [(x, y)]
        self.destroyed = 0
        self.created = []         # [type_index]
        self.placernd = []        # 分支 A：0x595400 依次收到的区域下标
        self.region_ptrs = []

        # 0x578460 读的可用矩形
        self.wr32(MAP + 0x0FC, 0)
        self.wr32(MAP + 0x100, 0)
        self.wr32(MAP + 0x104, w)
        self.wr32(MAP + 0x108, h)
        # 每格：未定砖、无占用者、+0xEC = 0、层高 0
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            self.wr32(c + 0x38, 0)
            self.wr32(c + 0xE4, 0)
            self.wr32(c + 0xEC, 0)
            self.uc.mem_write(c + 0x11B, b"\x00\x00")
        # 共享哑元 0xABDC50 清干净：菱形外的坐标（包括「摸 200 次之后强行用的
        # 格 (0,0)」）全部落在它身上，读到的就是这些值
        for off in range(0, CELL_SIZE, 4):
            self.wr32(0xABDC50 + off, 0)
        # 工作记录 +0x00：阶段 1 只给菱形内的格子填打包坐标
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE,
                      (y << 16) | x)

        # 假 RulesClass：+0xAE0 数据、+0xAEC 个数
        rules = self._bump(0xB00)
        types = self._bump(4 * max(type_count, 1))
        self.types = []
        for i in range(max(type_count, 1)):
            t = self._bump(16)
            vtb = self._bump(0x100)
            self.wr32(t, vtb)
            self.wr32(vtb + 0x90, V_FOUND)
            self.wr32(types + i * 4, t)
            self.types.append(t)
        self.wr32(rules + 0xAE0, types)
        self.wr32(rules + 0xAEC, type_count)
        self.wr32(RULES, rules)

        # 假建筑虚表 A
        self.vta = self._bump(0x200)
        self.wr32(self.vta + 0x20, V_DESTROY)
        self.wr32(self.vta + 0xD8, V_UNLIMBO)

        # 占地表：short 对，(0x7FFF, 0x7FFF) 收尾
        self.found = self._bump(4 * (len(foundation) + 1))
        for i, (dx, dy) in enumerate(foundation):
            self.uc.mem_write(self.found + i * 4, struct.pack("<hh", dx, dy))
        self.uc.mem_write(self.found + len(foundation) * 4,
                          struct.pack("<hh", 0x7FFF, 0x7FFF))
        self.foundation = list(foundation)

        for a in (V_FOUND, V_UNLIMBO, V_DESTROY):
            self.uc.mem_write(a, b"\xC3")
        self.uc.hook_add(UC_HOOK_CODE, self._h_findhousetype,
                         begin=A_FINDHOUSETYPE, end=A_FINDHOUSETYPE)
        self.uc.hook_add(UC_HOOK_CODE, self._h_findhouse,
                         begin=A_FINDHOUSE, end=A_FINDHOUSE)
        self.uc.hook_add(UC_HOOK_CODE, self._h_bldctor,
                         begin=A_BLDCTOR, end=A_BLDCTOR)
        self.uc.hook_add(UC_HOOK_CODE, self._h_found, begin=V_FOUND, end=V_FOUND)
        self.uc.hook_add(UC_HOOK_CODE, self._h_unlimbo,
                         begin=V_UNLIMBO, end=V_UNLIMBO)
        self.uc.hook_add(UC_HOOK_CODE, self._h_destroy,
                         begin=V_DESTROY, end=V_DESTROY)
        self.uc.hook_add(UC_HOOK_CODE, self._h_placernd,
                         begin=A_PLACERND, end=A_PLACERND)
        if new_fails:
            self.uc.hook_add(UC_HOOK_CODE, self._h_new_fail,
                             begin=A_NEW, end=A_NEW)

    # 分支 A 的区域表：[0xABDF94] 数据 / [0xABDFA0] 个数，只用到 +0x20
    def set_regions(self, starts):
        arr = self._bump(4 * max(len(starts), 1))
        self.region_ptrs = []
        for i, n in enumerate(starts):
            r = self._bump(0x40)
            self.wr32(r + 0x20, n)
            self.wr32(arr + i * 4, r)
            self.region_ptrs.append(r)
        self.wr32(0xABDF94, arr)
        self.wr32(0xABDFA0, len(starts))

    # ---- 桩 ----
    def _h_findhousetype(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)          # __fastcall，ecx 传参，调用方不收栈

    def _h_findhouse(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0x11110000)
        self._ret(uc, 0)          # 同上

    def _h_new_fail(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    def _h_bldctor(self, uc, address, size, ud):
        from unicorn.x86_const import (UC_X86_REG_ECX, UC_X86_REG_EAX,
                                       UC_X86_REG_ESP)
        this = uc.reg_read(UC_X86_REG_ECX)
        esp = uc.reg_read(UC_X86_REG_ESP)
        tp = self.rd32(esp + 4)
        self.created.append(self.types.index(tp) if tp in self.types else -1)
        self.wr32(this, self.vta)
        self.wr32(this + 0x520, tp)
        uc.reg_write(UC_X86_REG_EAX, this)
        self._ret(uc, 8)

    def _h_found(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, self.found)
        self._ret(uc, 4)

    def _h_unlimbo(self, uc, address, size, ud):
        from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX,
                                       UC_X86_REG_ESP)
        esp = uc.reg_read(UC_X86_REG_ESP)
        this = uc.reg_read(UC_X86_REG_ECX)
        p = self.rd32(esp + 4)
        cx = struct.unpack("<i", self.uc.mem_read(p, 4))[0]
        cy = struct.unpack("<i", self.uc.mem_read(p + 4, 4))[0]
        x, y = (cx - 0x80) >> 8, (cy - 0x80) >> 8
        if self.unlimbo_ok:
            self.placements.append((x, y))
            for (dx, dy) in self.foundation:
                c = self.cell(x + dx, y + dy)
                if c:
                    self.wr32(c + 0xE4, this)
        uc.reg_write(UC_X86_REG_EAX, 1 if self.unlimbo_ok else 0)
        self._ret(uc, 8)

    def _h_placernd(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX
        r = uc.reg_read(UC_X86_REG_ECX)
        self.placernd.append(self.region_ptrs.index(r))
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)          # __fastcall

    def _h_destroy(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        self.destroyed += 1
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 4)

    # ---- 环境布置 ----
    def set_cellv(self, x, y, tile=None, level=None, ec=None, e4=None,
                  flag45=None):
        c = self.cell(x, y)
        if tile is not None:
            self.wr32(c + 0x38, tile)
        if level is not None:
            self.uc.mem_write(c + 0x11B, bytes([level & 0xFF]))
        if ec is not None:
            self.wr32(c + 0xEC, ec)
        if e4 is not None:
            self.wr32(c + 0xE4, e4)
        if flag45 is not None:
            self.uc.mem_write(
                self.work + (y * self.side + x) * WORK_SIZE + 0x45,
                bytes([1 if flag45 else 0]))

    def run(self, map_type):
        seedobj = self._bump(0x400)
        self.wr32(seedobj + 0x3C, map_type)
        self.rng_calls = 0
        self._call(A_TECH, seedobj)
        occupied = []
        for (x, y) in self._diamond():
            if self.rd32(self.cell(x, y) + 0xE4):
                occupied.append((x, y))
        return self.rng_calls, list(self.placements), self.destroyed, occupied


W, H = 12, 8
F1 = ((0, 0),)
F4 = ((0, 0), (1, 0), (0, 1), (1, 1))


def build(kind, o):
    u"""按用例布置地图。"""
    cells = o._diamond()
    if kind in ("simple", "bigfoot", "unplaceable"):
        return
    if kind == "blocked":
        for (x, y) in cells:
            if (x * 5 + y * 3) % 7 == 0:
                o.set_cellv(x, y, tile=500)        # 已定砖 -> IsPlain 假
            elif (x * 3 + y) % 11 == 0:
                o.set_cellv(x, y, e4=0x22220000)   # 有占用者
        return
    if kind == "levels":
        for (x, y) in cells:
            o.set_cellv(x, y, level=(x + y) % 3)
        return
    if kind == "veto":
        for (x, y) in cells:
            if (x + y) % 5 == 0:
                o.set_cellv(x, y, ec=3)            # +0xEC == 3
            if (x * 7 + y) % 9 == 0:
                o.set_cellv(x, y, flag45=True)     # 工作记录 +0x45
        return
    if kind == "nowhere":
        # 全图已定砖：每次尝试都摸满 200 次，然后强行用格 (0,0)（哑元）
        for (x, y) in cells:
            o.set_cellv(x, y, tile=500)
        return
    raise AssertionError(kind)


# (用例, 随机种子, 占地表, Unlimbo 返回值, 建筑种类数)
CASES = [
    ("simple", 11, F1, True, 4),
    ("bigfoot", 12, F4, True, 3),
    ("blocked", 22, F4, True, 5),
    ("levels", 19, F4, True, 2),
    ("veto", 15, F1, True, 7),
    ("unplaceable", 16, F4, False, 4),
    ("nowhere", 17, F1, True, 4),
]


# 分支 A：(用例, 随机种子, 各区域的 +0x20)
ACASES = [
    ("regA-1", 1, [1, 0, 2, 0, 3]),
    ("regA-2", 3, [1, 0, 2, 0, 3]),     # n = 2，整表刷两遍
    ("regA-0", 4, [1, 2]),              # n = 0，一次都不刷（随机数照花）
    ("regA-none", 10, [0, 0, 0]),       # 没有区域分到出生点
    ("regA-empty", 15, []),             # 区域表本身是空的
]


def main():
    out = sys.stdout if len(sys.argv) < 2 else open(sys.argv[1], "w")
    out.write("# AddTechBuildings 0x5A95B0 分支 B 基准\n")
    out.write("# CASE name w h rngseed unlimbo typecount fnd "
              "-> rng placed destroyed\n")
    out.write("# IN   x y tile level ec e4 flag45  （与缺省 0 不同的格）\n")
    out.write("# TYPE k                            （每栋抽到的种类下标）\n")
    out.write("# PLACE x y                         （Unlimbo 的落点，按顺序）\n")
    out.write("# OCC  x y                          （跑完 +0xE4 非 0 的格）\n")
    for (kind, seed, fnd, unl, tc) in CASES:
        o = TechOracle(W, H, seed=seed, foundation=fnd, unlimbo=unl,
                       type_count=tc)
        build(kind, o)
        ins = []
        for (x, y) in o._diamond():
            c = o.cell(x, y)
            tile = o.rd32(c + 0x38)
            lv = struct.unpack("<b", o.uc.mem_read(c + 0x11B, 1))[0]
            ec = o.rd32(c + 0xEC)
            e4 = o.rd32(c + 0xE4)
            f45 = o.uc.mem_read(
                o.work + (y * o.side + x) * WORK_SIZE + 0x45, 1)[0]
            if tile or lv or ec or e4 or f45:
                ins.append((x, y, tile, lv, ec, 1 if e4 else 0, f45))
        rng, places, destroyed, occ = o.run(1)
        out.write("CASE %s %d %d %d %d %d %s -> %d %d %d\n" % (
            kind, W, H, seed, 1 if unl else 0, tc,
            ",".join("%d:%d" % p for p in fnd), rng, len(places), destroyed))
        for row in ins:
            out.write("IN %d %d %d %d %d %d %d\n" % row)
        for k in o.created:
            out.write("TYPE %d\n" % k)
        for (x, y) in places:
            out.write("PLACE %d %d\n" % (x, y))
        for (x, y) in occ:
            out.write("OCC %d %d\n" % (x, y))
        sys.stderr.write("%-12s rng=%-6d placed=%-3d destroyed=%-3d occ=%d\n"
                         % (kind, rng, len(places), destroyed, len(occ)))
    for (kind, seed, starts) in ACASES:
        o = TechOracle(W, H, seed=seed)
        o.set_regions(starts)
        rng, _, _, _ = o.run(2)
        out.write("ACASE %s %d %d %d %s -> %d %s\n" % (
            kind, W, H, seed, ",".join(str(v) for v in starts) or "-",
            rng, ",".join(str(v) for v in o.placernd) or "-"))
        sys.stderr.write("%-12s rng=%-6d 0x595400 x %d\n"
                         % (kind, rng, len(o.placernd)))
    if out is not sys.stdout:
        out.close()


if __name__ == "__main__":
    main()
