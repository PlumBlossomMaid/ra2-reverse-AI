# -*- coding: utf-8 -*-
u"""寻路分区 0x56C510（MapClass::Update_Pathfinding_1）的整函数模拟执行。

它不碰 CellClass，只吃 `MapClass+0x68` 那张 side x side 的 4 字节小表
（byte0 地面类别 / byte1 层高 / word2 区编号），产出：
  MapClass+0x4C        区数 + 1
  表里每格的 word2     区编号
  MapClass+0x18..0x48  13 张「区编号 -> 区类型」表
  返回值               格数最多的那个区的编号

所以基准可以完全绕开地形流水线：直接合成 land/level，跑真身，记结果。
洪泛 0x56CB90 的那几处怪癖（右扫门限是 4 不是 2、run_len 可能是 -1、
`[0xABDE8C]` 的更新时机）全在这条链路上，正是要对拍的东西。

伪造出来的运行环境（只读分析 gamemd.exe）：
  MapClass::Instance 0x87F7E8 的 +0x14 / +0x18..0x48 / +0x4C / +0x54 / +0x60 /
  +0x68 / +0x6C / +0xF4 / +0xF8
  跨区连接桶按 0x565840-0x5658DD 的原样摆：容器 0x10 字节 + 256 个 0x18 字节的
  DynamicVectorClass（虚表 0x7ED540、GrowthStep 被改写成 0x14）
拦截：malloc 0x7C8E17 / free 0x7C8B3D 换成 bump 分配器（free 是空操作）。
无 x87、无随机数，纯整数，Unicorn 在这条路径上完全可信。
"""
import struct
import sys

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x8000000
STACK_BASE = 0x10000000
STACK_SIZE = 0x00800000

A_MALLOC = 0x7C8E17
A_FREE = 0x7C8B3D
A_UPDATE = 0x56C510

MAP = 0x87F7E8
BUCKET_VT = 0x7ED540


class ZoneOracle(object):
    def __init__(self, w, h):
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
        self.w, self.h = w, h
        self.side = w + h + 1
        self.count = self.side * self.side

        uc.hook_add(UC_HOOK_CODE, self._h_malloc, begin=A_MALLOC, end=A_MALLOC)
        uc.hook_add(UC_HOOK_CODE, self._h_free, begin=A_FREE, end=A_FREE)

        # 区表：0x567110-0x567182 的初值是 land=7 / level=0，区编号未初始化
        self.table = self._bump(self.count * 4)
        for i in range(self.count):
            uc.mem_write(self.table + i * 4, b"\x07\x00\xCD\xAB")

        # 跨区连接桶：照抄 0x565840-0x5658DD
        cont = self._bump(0x10)
        raw = self._bump(0x1804)
        self.wr32(raw, 0x100)
        buckets = raw + 4
        for i in range(0x100):
            b = buckets + i * 0x18
            self.wr32(b + 0x00, BUCKET_VT)       # 0x565896
            self.wr32(b + 0x04, 0)               # Vector
            self.wr32(b + 0x08, 0)               # VectorMax
            uc.mem_write(b + 0x0C, b"\x01\x00")  # IsValid=1 / IsAllocated=0
            self.wr32(b + 0x10, 0)               # Count      0x5658A3
            self.wr32(b + 0x14, 0x14)            # GrowthStep 0x5658CC 改成 20
        self.wr32(cont + 0x00, buckets)
        self.wr32(cont + 0x04, 0x56CB80)
        self.wr32(cont + 0x08, 0x100)
        self.wr32(cont + 0x0C, 0x14)

        for off in range(0x00, 0x150, 4):
            self.wr32(MAP + off, 0)
        self.wr32(MAP + 0x14, cont)
        self.wr32(MAP + 0x54, 0)      # 桥/隧道表
        self.wr32(MAP + 0x60, 0)      # 桥数 0，第 (5) 步整段空转
        self.wr32(MAP + 0x68, self.table)
        self.wr32(MAP + 0x6C, self.count)
        self.wr32(MAP + 0xF4, w)
        self.wr32(MAP + 0xF8, h)

    # ---- 基础设施 ----
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

    def rd16(self, a):
        return struct.unpack("<H", self.uc.mem_read(a, 2))[0]

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

    # ---- 跑 ----
    def set_cells(self, cells):
        buf = bytearray()
        for land, level in cells:
            buf += struct.pack("<BBH", land & 0xFF, level & 0xFF, 0xABCD)
        self.uc.mem_write(self.table, bytes(buf))

    def run(self):
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX,
                                       UC_X86_REG_EAX)
        sp = STACK_BASE + STACK_SIZE - 0x1000
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", MAGIC_RET))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, MAP)
        self.uc.emu_start(A_UPDATE, MAGIC_RET)
        ret = self.uc.reg_read(UC_X86_REG_EAX)
        zcnt = self.rd32(MAP + 0x4C)
        zones = [self.rd16(self.table + i * 4 + 2) for i in range(self.count)]
        types = []
        for k in range(13):
            p = self.rd32(MAP + 0x18 + k * 4)
            types.append([self.rd16(p + z * 2) for z in range(zcnt)])
        return ret, zcnt, zones, types


# ── 合成地形 ───────────────────────────────────────────────────────────────
LANDS = (0, 3, 4, 6)          # Clear / Beach / Water / Impassable


def synth(kind, w, h):
    side = w + h + 1
    out = []
    st = 0x12345678
    for i in range(side * side):
        y, x = divmod(i, side)
        st = (st * 1103515245 + 12345) & 0xFFFFFFFF
        r = st >> 16
        inmap = (x + y > w and x - y < w and y - x < w and x + y <= w + 2 * h)
        if not inmap:
            out.append((7, 0))
        elif kind == "flat":
            out.append((0, 0))
        elif kind == "bands":
            out.append((0, ((x + y) // 4) % 5))
        elif kind == "twoland":
            out.append((0 if ((x // 3 + y // 3) & 1) else 4, 0))
        elif kind == "levels":
            out.append((0, (x // 2 + y // 3) % 6))
        elif kind == "rocky":
            out.append((6 if (r % 5 == 0) else 0, r % 3))
        elif kind == "chaos":
            out.append((LANDS[r % 4], r % 4))
        elif kind == "chaos2":
            out.append((LANDS[r % 4], (r >> 3) % 7))
        elif kind == "ramp":
            out.append((0, abs(((x + 2 * y) % 16) - 8)))
        elif kind == "ramp2":
            out.append((4 if (r % 9 == 0) else 0, abs(((x + 2 * y) % 16) - 8)))
        elif kind == "ramp3":
            out.append((LANDS[r % 4] if (r % 11 == 0) else 0,
                        abs(((x + 5 * y) % 24) - 12)))
        elif kind == "holes":
            out.append((7 if (r % 7 == 0) else LANDS[r % 4], r % 2))
        else:
            raise SystemExit("unknown kind " + kind)
    return out


CASES = [
    ("flat-6x6", "flat", 6, 6),
    ("bands-6x6", "bands", 6, 6),
    ("twoland-8x8", "twoland", 8, 8),
    ("levels-8x6", "levels", 8, 6),
    ("rocky-10x10", "rocky", 10, 10),
    ("chaos-10x10", "chaos", 10, 10),
    ("chaos2-12x9", "chaos2", 12, 9),
    ("holes-12x12", "holes", 12, 12),
    ("ramp-12x12", "ramp", 12, 12),
    ("ramp2-12x12", "ramp2", 12, 12),
    ("ramp3-14x11", "ramp3", 14, 11),
    ("chaos-20x20", "chaos", 20, 20),
    ("chaos2-3x3", "chaos2", 3, 3),
    ("flat-2x2", "flat", 2, 2),
]


def main():
    out = sys.stdout
    if len(sys.argv) > 1:
        out = open(sys.argv[1], "w")
    out.write("# 0x56C510 MapClass::Update_Pathfinding_1 golden\n")
    out.write("# CASE <name> <w> <h> <count>\n")
    out.write("# LAND/LVL 每格两位十六进制；ZONE/MZk 每项四位十六进制\n")
    for name, kind, w, h in CASES:
        o = ZoneOracle(w, h)
        cells = synth(kind, w, h)
        assert len(cells) == o.count
        o.set_cells(cells)
        ret, zcnt, zones, types = o.run()
        out.write("CASE %s %d %d %d\n" % (name, w, h, o.count))
        out.write("LAND %s\n" % "".join("%02x" % c[0] for c in cells))
        out.write("LVL %s\n" % "".join("%02x" % c[1] for c in cells))
        out.write("RET %d\n" % struct.unpack("<i", struct.pack("<I", ret))[0])
        out.write("ZCNT %d\n" % zcnt)
        out.write("ZONE %s\n" % "".join("%04x" % z for z in zones))
        for k in range(13):
            out.write("MZ%d %s\n" % (k, "".join("%04x" % v for v in types[k])))
        sys.stderr.write("%-14s zones=%-5d largest=%d\n" % (name, zcnt, ret))
    if out is not sys.stdout:
        out.close()


if __name__ == "__main__":
    main()
