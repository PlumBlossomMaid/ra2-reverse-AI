# -*- coding: utf-8 -*-
"""八邻域「水」掩码 0x57B210 的整函数模拟执行 —— 取黄金基准。

掩码是岸线平滑四遍扫描的公共底座，四遍的每一条判定都读它，
所以它错一位整条岸线就走样。这里把它单独拎出来穷举对拍。

伪造的运行环境与 dump_blob_paint_golden.py 同源：
  MapClass 0x87F7E8 +0xF4/+0xF8 尺寸、+0x13C 格子指针数组
  工作数组 [0xABED10]（+0x40 掩码缓存 / +0x4A 有效位）
  边界 [0xABED04]/[0xABED08]、side [0x89C2DC]、水面组基 [0xAA0738]
本函数不碰浮点、不取随机数，所以没有 fyl2x 那类保留。
"""
import random
import struct
import sys

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x4000000
STACK_BASE = 0x20000000
STACK_SIZE = 0x100000

CELL_SIZE = 0x180
WORK_SIZE = 0x50
WATER = 0x100          # 随便挑一个水面组基下标，判水区间即 [0x100, 0x10E)
A_MASK = 0x57B210


class MaskOracle(object):
    def __init__(self, w, h):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_FPCW

        data, base, image_size, secs = load_sections(EXE)
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(IMAGE_BASE, (image_size + 0xFFF) & ~0xFFF)
        for name, vaddr, vsize, roff, rsize in secs:
            if rsize:
                uc.mem_write(IMAGE_BASE + vaddr, data[roff:roff + rsize])
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_map(SCRATCH, SCRATCH_SIZE)
        uc.mem_map(MAGIC_RET & ~0xFFF, 0x1000)
        uc.reg_write(UC_X86_REG_FPCW, 0x0E7F)
        self.uc = uc
        self.w, self.h = w, h
        self.side = w + h + 1
        self.brk = SCRATCH

        self.cellptr = self._bump(0x40000 * 4)
        self.work = self._bump(self.side * self.side * WORK_SIZE)
        self.cells = {}
        for (x, y) in self.diamond():
            c = self._bump(CELL_SIZE)
            self.wr32(self.cellptr + ((y << 9) + x) * 4, c)
            self.wr32(c + 0x24, (y << 16) | x)
            self.cells[(x, y)] = c

        self.wr32(0x87F7E8 + 0xF4, w)
        self.wr32(0x87F7E8 + 0xF8, h)
        self.wr32(0x87F7E8 + 0x13C, self.cellptr)
        self.wr32(0x89C2DC, self.side)
        self.wr32(0xABED04, w)
        self.wr32(0xABED08, w + 2 * h)
        self.wr32(0xABED10, self.work)
        self.wr32(0xAA0738, WATER)

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

    def diamond(self):
        w, h = self.w, self.h
        return [(x, y)
                for y in range(0, w + 2 * h + 2)
                for x in range(0, w + 2 * h + 2)
                if x + y > w and x - y < w and y - x < w and x + y <= w + 2 * h]

    def work_rec(self, x, y):
        return self.work + (y * self.side + x) * WORK_SIZE

    def set_tile(self, x, y, tile):
        self.wr32(self.cells[(x, y)] + 0x38, tile)

    def set_work(self, x, y, valid, cache):
        r = self.work_rec(x, y)
        self.uc.mem_write(r + 0x4A, bytes([1 if valid else 0]))
        self.wr32(r + 0x40, cache)

    def mask(self, x, y, mode):
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX,
                                       UC_X86_REG_EAX)
        sp = STACK_BASE + STACK_SIZE - 0x4000
        for v in reversed((self.cells[(x, y)], mode)):
            sp -= 4
            self.wr32(sp, v)
        sp -= 4
        self.wr32(sp, MAGIC_RET)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, 0x87F7E8)
        self.uc.emu_start(A_MASK, MAGIC_RET)
        return self.uc.reg_read(UC_X86_REG_EAX)


def main():
    out = []
    for w, h in ((10, 10), (14, 9)):
        o = MaskOracle(w, h)
        cells = o.diamond()
        rnd = random.Random(0xC0FFEE ^ (w * 131 + h))
        # 三类瓦片：水（[0x100,0x10E)）、未定砖（0 / 0xFFFF）、陆地（其它）
        pool = [WATER + i for i in range(0x0E)] + [0, 0xFFFF, 0x50, 0x200, 0x2000]
        for (x, y) in cells:
            o.set_tile(x, y, rnd.choice(pool))
            # 大部分记录有效、缓存失效；掺一成无效位和一成缓存命中
            r = rnd.random()
            o.set_work(x, y, r > 0.10, rnd.randrange(0, 0x100) if r > 0.90 else -1)
        out.append("MAP %d %d" % (w, h))
        for (x, y) in cells:
            t = o.rd32(o.cells[(x, y)] + 0x38)
            rec = o.work_rec(x, y)
            valid = o.uc.mem_read(rec + 0x4A, 1)[0]
            cache = struct.unpack("<i", o.uc.mem_read(rec + 0x40, 4))[0]
            out.append("CELL %d %d %d %d %d" % (x, y, t, valid, cache))
        for (x, y) in cells:
            for mode in (0, 1, 2):
                out.append("MASK %d %d %d -> %d" % (x, y, mode, o.mask(x, y, mode)))
    path = r"E:\code\ra2-reverse-AI\memory\data\rmg\shore_mask_golden.txt"
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
