# -*- coding: utf-8 -*-
u"""ChooseWaypoints 0x594F40（阶段 6 的最远点采样）的整函数模拟执行。

它一个随机数都不取，只吃三样东西：
  * 一个 DynamicVectorClass<CellStruct>（候选表），栈上传进来
  * ecx 指向的 RegionClass，只读 +0x20（分到的出生点个数）
  * 工作数组 [0xABED10] 里每格的 +0x38（区域编号）——用来给「跨区」加 20 分
再加两个标量 [0xABE030]（waypoint 系数）与 [0xABE028]（玩家数）。

产出是一个新的向量，逐个 dword 记下来即可。想对拍的是那几处怪癖：
  * want 的两次兜底改写，以及 startCount == 0 时 `return NULL`
  * 候选只有一个时**直接返回**、不理会 want
  * 已选中的候选不会被剔除，名额没用完时会被**重复选中**
  * 距离用的是查表开方 0x4CAC40，不是精确 sqrt

拦截：malloc 0x7C8E17 / free 0x7C8B3D 换成 bump 分配器。
函数体里没有随机数、没有 x87 超越函数，只有 fld/fadd/fcomp 与 FastSqrt 查表，
Unicorn 在这条路径上可信。
"""
import struct
import sys

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x2000000
STACK_BASE = 0x10000000
STACK_SIZE = 0x00100000

A_MALLOC = 0x7C8E17
A_FREE = 0x7C8B3D
A_CHOOSE = 0x594F40

G_WORK = 0xABED10        # 工作数组基址
G_SIDE = 0x89C2DC        # side = W + H + 1
G_SCALE = 0xABE030       # MapSeedClass +0x58
G_PLAYERS = 0xABE028     # MapSeedClass +0x50
VT_VECTOR = 0x7E3890     # DynamicVectorClass<CellStruct> 虚表


class WaypointOracle(object):
    def __init__(self, side):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE

        data, base, image_size, secs = load_sections(EXE)
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(IMAGE_BASE, (image_size + 0xFFF) & ~0xFFF)
        for name, vaddr, vsize, roff, rsize in secs:
            if rsize:
                uc.mem_write(IMAGE_BASE + vaddr, data[roff:roff + rsize])
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_map(SCRATCH, SCRATCH_SIZE)
        uc.mem_map(MAGIC_RET & ~0xFFF, 0x1000)
        self.uc = uc
        self.brk = SCRATCH
        self.side = side

        uc.hook_add(UC_HOOK_CODE, self._h_malloc, begin=A_MALLOC, end=A_MALLOC)
        uc.hook_add(UC_HOOK_CODE, self._h_free, begin=A_FREE, end=A_FREE)

        self.work = self._bump(side * side * 0x50)
        self.wr32(G_WORK, self.work)
        self.wr32(G_SIDE, side)

    def _bump(self, n):
        p = self.brk
        self.brk = (self.brk + n + 15) & ~15
        assert self.brk < SCRATCH + SCRATCH_SIZE, "scratch exhausted"
        self.uc.mem_write(p, b"\0" * n)
        return p

    def wr32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def rd32(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def _ret(self, uc, pop):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, self.rd32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        uc.reg_write(UC_X86_REG_EAX, self._bump(n if n else 1))
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    def set_owner(self, x, y, owner):
        self.wr32(self.work + (y * self.side + x) * 0x50 + 0x38, owner)

    def run(self, cand, start_count, scale, players):
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX,
                                       UC_X86_REG_EAX)
        self.wr32(G_SCALE, scale)
        self.wr32(G_PLAYERS, players)

        n = len(cand)
        cap = n if n else 1
        arr = self._bump(cap * 4)
        for i, (x, y) in enumerate(cand):
            self.wr32(arr + i * 4, ((y & 0xFFFF) << 16) | (x & 0xFFFF))
        vec = self._bump(0x18)
        self.wr32(vec + 0x00, VT_VECTOR)
        self.wr32(vec + 0x04, arr)
        self.wr32(vec + 0x08, cap)
        self.uc.mem_write(vec + 0x0C, b"\x01\x01")   # IsValid / IsAllocated
        self.wr32(vec + 0x10, n)
        self.wr32(vec + 0x14, 10)

        region = self._bump(0x50)
        self.wr32(region + 0x20, start_count)

        sp = STACK_BASE + STACK_SIZE - 0x2000
        sp -= 4
        self.wr32(sp, vec)
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", MAGIC_RET))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, region)
        self.uc.emu_start(A_CHOOSE, MAGIC_RET)
        out = self.uc.reg_read(UC_X86_REG_EAX)
        if out == 0:
            return None
        cnt = self.rd32(out + 0x10)
        data = self.rd32(out + 0x04)
        pts = []
        for i in range(cnt):
            v = self.rd32(data + i * 4)
            pts.append((v & 0xFFFF, (v >> 16) & 0xFFFF))
        return pts


# ── 合成候选表 ─────────────────────────────────────────────────────────────
SIDE = 160


def synth(kind, n):
    st = 0x2468ACE0
    pts, owners = [], []
    for i in range(n):
        st = (st * 1103515245 + 12345) & 0xFFFFFFFF
        r = st >> 16
        if kind == "line":
            pts.append((10 + i * 3, 40))
            owners.append(1)
        elif kind == "cluster":
            pts.append((30 + (r % 5), 30 + ((r >> 3) % 5)))
            owners.append(1)
        elif kind == "two":                 # 两团，分属两个区域
            g = i & 1
            pts.append((20 + g * 60 + (r % 7), 25 + (r >> 4) % 7))
            owners.append(1 + g)
        elif kind == "spread":
            pts.append((5 + (r % 120), 5 + ((r >> 7) % 120)))
            owners.append(1 + (r % 3))
        elif kind == "dup":                 # 大量重合点
            pts.append((40 + (i % 3) * 10, 40))
            owners.append(1)
        else:
            raise SystemExit("unknown kind " + kind)
    return pts, owners


CASES = []
for kind in ("line", "cluster", "two", "spread", "dup"):
    for n in (0, 1, 2, 3, 7, 20):
        for sc in (0, 1, 3):
            for scale, players in ((0, 2), (25, 4), (100, 8)):
                CASES.append((kind, n, sc, scale, players))


def main():
    out = []
    oracle = WaypointOracle(SIDE)
    for kind, n, sc, scale, players in CASES:
        pts, owners = synth(kind, n)
        # 每例都把工作数组的区域编号重铺一遍（不同用例的点集会重叠）
        for (x, y), o in zip(pts, owners):
            oracle.set_owner(x, y, o)
        got = oracle.run(pts, sc, scale, players)
        out.append("CASE %s %d %d %d %d" % (kind, n, sc, scale, players))
        out.append("CAND " + (",".join("%d:%d:%d" % (x, y, o)
                                       for (x, y), o in zip(pts, owners))
                              if pts else "-"))
        if got is None:
            out.append("NULL")
        else:
            out.append("OUT " + (",".join("%d:%d" % p for p in got)
                                 if got else "-"))
    path = sys.argv[1] if len(sys.argv) > 1 else "start_points_golden.txt"
    with open(path, "w") as f:
        f.write("# ChooseWaypoints 0x594F40 黄金基准（Unicorn 跑原版机器码）\n")
        f.write("# 由 code/randmap/dump_start_points_golden.py 生成\n")
        f.write("\n".join(out) + "\n")
    print("%d cases -> %s" % (len(CASES), path))


if __name__ == "__main__":
    main()
