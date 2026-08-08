# -*- coding: utf-8 -*-
u"""阶段 8 内层 0x5A28C0 CreateOreField 的整函数模拟执行。

它是自足的：往下只调 malloc/free、格子迭代器（0x578350/0x578290）、
0x578460 IsWithinUsableArea、0x5657A0 GetCellAt、0x486380 未定砖、
0x5AD870 堆下沉，以及三处取数点。四个外部依赖用桩换掉：

  0x485010 GetContainedTiberiumIndex —— 真身要走 rules.ini 的 TiberiumType 表
           （0x5FDD20 遍历 [0xB0F4EC]），与本函数的逻辑无关。桩按「格子上是
           矿(102..113)或宝石(27..38)就返回 0，否则 -1」给，C++ 那边的
           OreWorld::ContainedTiberiumIndex 用同一条规则。
  0x7C8EF4 sprintf   —— 自己格式化 "TIBTRE0%d"，顺便把名字记下来
  0x71DD80 FindIndex —— 恒返回 0，并记下传进来的名字
  0x71BB90 TerrainClass::TerrainClass —— 空操作，只记 (名字, 落点)

⚠️ 池子没有上界检查（见 stage-tiberium.md §3.3），一旦 used 越过 count*10
原版就写到分配区外面去了——那是真·未定义行为，不能拿来当基准。所以这里
钩住写入点 0x5A2D40 盯着 used，任何一个用例只要越界就直接报错。

基准收：跑完之后每个格子的 (overlay, overlayData)、工作记录 +0x3C、
随机数个数，以及建了哪些 TIBTRE 地貌物。
"""
import struct
import sys

from dump_place_tile_golden import PlaceOracle
from dump_blob_paint_golden import WORK_SIZE, CELL_SIZE

A_CREATE = 0x5A28C0
A_TIBIDX = 0x485010
A_FINDIDX = 0x71DD80
A_TERRAIN = 0x71BB90
A_SPRINTF = 0x7C8EF4
A_POOLWR = 0x5A2D40      # mov [edi], ecx —— 池子的唯一写入点
DUMMY = 0xABDC50
MAP = 0x87F7E8


class OreOracle(PlaceOracle):
    def __init__(self, w, h, seed=1):
        PlaceOracle.__init__(self, w, h, seed)
        # 0x578460 读的可用矩形：+0xFC/+0x100 原点、+0x104/+0x108 宽高
        self.wr32(MAP + 0x0FC, 0)
        self.wr32(MAP + 0x100, 0)
        self.wr32(MAP + 0x104, w)
        self.wr32(MAP + 0x108, h)
        # 每格：砖号 0（未定砖）、overlay -1、overlayData 0
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            self.wr32(c + 0x38, 0)
            self.wr32(c + 0x44, 0xFFFFFFFF)
            self.uc.mem_write(c + 0x11E, b"\x00")
        for off in range(0, CELL_SIZE, 4):
            self.wr32(DUMMY + off, 0)
        self.wr32(DUMMY + 0x44, 0xFFFFFFFF)
        # TerrainTypes [0xA8E31C]：桩只要求这张表能取到一个指针
        tt = self._bump(64)
        self.wr32(tt, self._bump(16))
        self.wr32(0xA8E31C, tt)
        self.terrains = []
        self.pool_lo = 0
        self.pool_hi = 0
        self.max_used = 0
        from unicorn import UC_HOOK_CODE
        self.uc.hook_add(UC_HOOK_CODE, self._h_tibidx, begin=A_TIBIDX, end=A_TIBIDX)
        self.uc.hook_add(UC_HOOK_CODE, self._h_sprintf, begin=A_SPRINTF, end=A_SPRINTF)
        self.uc.hook_add(UC_HOOK_CODE, self._h_findidx, begin=A_FINDIDX, end=A_FINDIDX)
        self.uc.hook_add(UC_HOOK_CODE, self._h_terrain, begin=A_TERRAIN, end=A_TERRAIN)
        self.uc.hook_add(UC_HOOK_CODE, self._h_poolwr, begin=A_POOLWR, end=A_POOLWR)

    # ---- 四个桩 ----
    def _h_tibidx(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX
        ov = struct.unpack("<i", self.uc.mem_read(
            uc.reg_read(UC_X86_REG_ECX) + 0x44, 4))[0]
        hit = (102 <= ov <= 113) or (27 <= ov <= 38)
        uc.reg_write(UC_X86_REG_EAX, 0 if hit else 0xFFFFFFFF)
        self._ret(uc, 0)

    def _h_sprintf(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        esp = uc.reg_read(UC_X86_REG_ESP)
        buf = self.rd32(esp + 4)
        arg = self.rd32(esp + 0xC)
        s = ("TIBTRE0%d" % arg).encode("ascii")
        self.uc.mem_write(buf, s + b"\0")
        uc.reg_write(UC_X86_REG_EAX, len(s))
        self._ret(uc, 0)                      # cdecl，调用方自己收栈

    def _h_findidx(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX
        p = uc.reg_read(UC_X86_REG_ECX)
        raw = bytes(self.uc.mem_read(p, 16))
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

    def _h_poolwr(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EDI
        used = (uc.reg_read(UC_X86_REG_EDI) - self.pool_lo) // 8
        if used > self.max_used:
            self.max_used = used

    # ---- 环境布置 ----
    def set_tile(self, x, y, v):
        self.wr32(self.cell(x, y) + 0x38, v)

    def set_ore(self, x, y, overlay, data):
        c = self.cell(x, y)
        self.wr32(c + 0x44, overlay & 0xFFFFFFFF)
        self.uc.mem_write(c + 0x11E, bytes([data & 0xFF]))

    def set_flag45(self, x, y, v):
        self.uc.mem_write(self.work + (y * self.side + x) * WORK_SIZE + 0x45,
                          bytes([1 if v else 0]))

    # ---- 观测 ----
    def snapshot(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            out[(x, y)] = (
                struct.unpack("<i", self.uc.mem_read(c + 0x44, 4))[0],
                self.uc.mem_read(c + 0x11E, 1)[0],
                struct.unpack("<i", self.uc.mem_read(
                    self.work + (y * self.side + x) * WORK_SIZE + 0x3C, 4))[0])
        return out

    def run(self, seed_xy, count, field_id, is_gems):
        p = self._bump(8)
        self.uc.mem_write(p, struct.pack("<hh", seed_xy[0], seed_xy[1]))
        # 池子第一次 malloc 的落点：bump 分配器是顺序的，先记下当前 brk
        self.pool_lo = (self.brk + 15) & ~15
        before = self.snapshot()
        self.rng_calls = 0
        self.max_used = 0
        self._call(A_CREATE, 0, (p, count, field_id, 1 if is_gems else 0))
        after = self.snapshot()
        diff = sorted((k, after[k]) for k in after if after[k] != before[k])
        assert self.max_used < count * 10, \
            "池子越界：used=%d cap=%d" % (self.max_used, count * 10)
        return diff, self.rng_calls, self.max_used, list(self.terrains)


W, H = 12, 8
SEED = (10, 10)


def build(kind, o):
    u"""按用例布置地图，返回 (seed, count, fieldId, isGems)。"""
    cells = o._diamond()

    if kind == "plain":
        return SEED, 20, 1, False

    if kind == "gems":
        return SEED, 20, 3, True

    if kind == "anchor":
        # 一条斜带打上 +0x45：这些格子不落矿、也不触发「爆发锚定」，
        # 但照样展开 8 邻居
        for (x, y) in cells:
            if (x + y) % 3 == 0:
                o.set_flag45(x, y, 1)
        return SEED, 20, 5, False

    if kind == "preore":
        # 已有矿的四格：走「加厚 +0x11E」那条，且第二条接受条件（没有
        # fieldId 保护的那条）会让它们被反复入队
        for i, (dx, dy) in enumerate(((2, 0), (2, 1), (3, 0), (3, 1))):
            o.set_ore(SEED[0] + dx, SEED[1] + dy, 102 + i * 3, i * 4)
        return SEED, 25, 7, False

    if kind == "island":
        # 只留种子格与它东边一格是「未定砖」，其余全填成已定砖 500。
        # 两格吃满之后扩散就断了 -> 冷启动重来 -> 撞上 restarts >= 10 的上限
        keep = {SEED, (SEED[0] + 1, SEED[1])}
        for (x, y) in cells:
            if (x, y) not in keep:
                o.set_tile(x, y, 500)
        return SEED, 40, 9, False

    if kind == "edge":
        # 菱形东端：邻格大量落在可用区外，走 0x578460 判否那条
        sx, sy = W + 2 * H - 3, 1
        while not (sx + sy > W and sx - sy < W and sy - sx < W and
                   sx + sy <= W + 2 * H):
            sx -= 1
        return (sx, sy), 15, 11, False

    if kind == "blocked":
        for (x, y) in cells:
            if (x * 7 + y * 3) % 5 == 0 and (x, y) != SEED:
                o.set_tile(x, y, 500)
        return SEED, 30, 13, False

    raise AssertionError(kind)


CASES = ["plain", "gems", "anchor", "preore", "island", "edge", "blocked"]


def main():
    out = sys.stdout if len(sys.argv) < 2 else open(sys.argv[1], "w")
    out.write("# CreateOreField 0x5A28C0 基准\n")
    out.write("# CASE name w h seedx seedy count field gems rngseed"
              " -> rng used changed\n")
    out.write("# IN   x y tile overlay data flag45   （与缺省 0/-1/0/0 不同的格）\n")
    out.write("# TERR name x y\n")
    out.write("# OUT  x y overlay data queued        （跑完发生变化的格）\n")
    for i, kind in enumerate(CASES):
        rng_seed = 1 + i
        o = OreOracle(W, H, seed=rng_seed)
        seed_xy, count, field, gems = build(kind, o)
        ins = []
        for (x, y) in o._diamond():
            c = o.cell(x, y)
            tile = o.rd32(c + 0x38)
            ov = struct.unpack("<i", o.uc.mem_read(c + 0x44, 4))[0]
            data = o.uc.mem_read(c + 0x11E, 1)[0]
            f45 = o.uc.mem_read(
                o.work + (y * o.side + x) * WORK_SIZE + 0x45, 1)[0]
            if tile or ov != -1 or data or f45:
                ins.append((x, y, tile, ov, data, f45))
        diff, rng, used, terr = o.run(seed_xy, count, field, gems)
        out.write("CASE %s %d %d %d %d %d %d %d %d -> %d %d %d\n" % (
            kind, W, H, seed_xy[0], seed_xy[1], count, field,
            1 if gems else 0, rng_seed, rng, used, len(diff)))
        for row in ins:
            out.write("IN %d %d %d %d %d %d\n" % row)
        for name, tx, ty in terr:
            out.write("TERR %s %d %d\n" % (name, tx, ty))
        for (x, y), (ov, data, q) in diff:
            out.write("OUT %d %d %d %d %d\n" % (x, y, ov, data, q))
        sys.stderr.write("%-8s rng=%-4d used=%-4d changed=%-4d terr=%d\n" % (
            kind, rng, used, len(diff), len(terr)))
    if out is not sys.stdout:
        out.close()


if __name__ == "__main__":
    main()
