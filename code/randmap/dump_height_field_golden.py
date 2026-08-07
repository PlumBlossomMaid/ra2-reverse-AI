# -*- coding: utf-8 -*-
u"""阶段 9 高度场 0x5A2F50 的整函数模拟执行。

造山阶段的随机性全在这一个函数里（每个可用格子抽两次截断正态）。它只吃：
  * 工作数组 [0xABED10]（每条 0x50 字节：+0x00 打包坐标 / +0x08 height /
    +0x10 slope / +0x45 锚点）
  * side [0x89C2DC]、菱形边界 [0xABED04] / [0xABED08]
  * MapSeedClass +0x44 = Ruggedness
  * 生成器自己的 Randomizer 0xABE890 与 NormalDeviate 对象 0xABDFB8
产出就是整张工作数组里的 height / slope。

路径上有 x87（fmul/fadd/fcomp/fsqrt/fyl2x —— NormalDeviate 走 Box-Muller，
里面有 ChopLog）。⚠️ Unicorn 的 fyl2x 已知不可信（见 fast-math.md §4.1），
所以**基准里只收 height / slope 的最终值**，并在测试侧允许 ChopLog 的
1 ulp 差异不影响结果（实测量化到 2^-14 的 FastSqrt 之后早已看不出来）。
若发现分歧，先怀疑模拟器而不是重写。
"""
import struct
import sys

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x4000000
STACK_BASE = 0x10000000
STACK_SIZE = 0x00100000

A_MALLOC = 0x7C8E17
A_FREE = 0x7C8B3D
A_HEIGHT = 0x5A2F50
A_SEED = 0x65C6D0

G_WORK = 0xABED10
G_SIDE = 0x89C2DC
G_D1 = 0xABED04          # W
G_D2 = 0xABED08          # W + 2H
MAPSEED = 0xABDFD8
RANDOMIZER = 0xABE890
NORMAL = 0xABDFB8
A_UNIFORM = 0x598000


class HeightOracle(object):
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
        uc.hook_add(UC_HOOK_CODE, self._h_malloc, begin=A_MALLOC, end=A_MALLOC)
        uc.hook_add(UC_HOOK_CODE, self._h_free, begin=A_FREE, end=A_FREE)

        self.w, self.h = w, h
        self.side = w + h + 1
        self.n = self.side * self.side
        self.work = self._bump(self.n * 0x50)
        self.wr32(G_WORK, self.work)
        self.wr32(G_SIDE, self.side)
        self.wr32(G_D1, w)
        self.wr32(G_D2, w + 2 * h)
        # NormalDeviate 对象：缓存清空 + 均匀源指向 0x598000
        self.wr32(NORMAL + 0x00, 0)
        self.uc.mem_write(NORMAL + 0x08, struct.pack("<d", 0.0))
        self.wr32(NORMAL + 0x10, A_UNIFORM)

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

    def rdf(self, a):
        return struct.unpack("<d", self.uc.mem_read(a, 8))[0]

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

    def _call(self, addr, ecx=None, args=()):
        from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_ECX,
                                       UC_X86_REG_EAX, UC_X86_REG_FPCW)
        # ⚠️ 必须先把 FPU 控制字摆成原版那份 0x0E7F（PC=53 + RC=chop）。
        # 漏了这一步的话 10 * 0.0025 会round-to-nearest 成正好 0.025，
        # 于是 Ruggedness == 10 那条边界用例会走进本该跳过的分支。
        self.uc.reg_write(UC_X86_REG_FPCW, 0x0E7F)
        sp = STACK_BASE + STACK_SIZE - 0x4000
        for a in reversed(args):
            sp -= 4
            self.wr32(sp, a)
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", MAGIC_RET))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        if ecx is not None:
            self.uc.reg_write(UC_X86_REG_ECX, ecx)
        self.uc.emu_start(addr, MAGIC_RET)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def inmap(self, x, y):
        return (x + y > self.w and x - y < self.w and y - x < self.w
                and x + y <= self.w + 2 * self.h)

    def run(self, ruggedness, seed, anchors):
        # 工作数组：菱形内的槽位填打包坐标，其余留 (0,0)
        self.uc.mem_write(self.work, b"\0" * (self.n * 0x50))
        for y in range(self.side):
            for x in range(self.side):
                if not self.inmap(x, y):
                    continue
                p = self.work + (y * self.side + x) * 0x50
                self.wr32(p + 0x00, ((y & 0xFFFF) << 16) | (x & 0xFFFF))
        for (x, y, hgt) in anchors:
            p = self.work + (y * self.side + x) * 0x50
            self.uc.mem_write(p + 0x45, b"\x01")
            self.uc.mem_write(p + 0x08, struct.pack("<d", hgt))

        self.wr32(NORMAL + 0x00, 0)
        self._call(A_SEED, ecx=RANDOMIZER, args=(seed,))
        self.wr32(MAPSEED + 0x44, ruggedness)
        self._call(A_HEIGHT, ecx=MAPSEED)

        out = []
        for i in range(self.n):
            p = self.work + i * 0x50
            out.append((self.rdf(p + 0x08), self.rdf(p + 0x10)))
        return out


CASES = [
    # (w, h, ruggedness, seed, 锚点)
    ("tiny",   6,  5,   0,      0x1111, []),                 # Ruggedness 0：整段跳过
    ("under",  6,  5,   9,      0x1111, []),                 # 9 < 10：仍然整段跳过
    ("edge",   6,  5,   10,     0x2222, []),                 # 恰好 10：baseSlope == 0.025
    ("small",  8,  6,   60,     0x1234, [(6, 4, 0.5), (9, 7, 0.5)]),
    ("mid",   10,  8,   200,    0xBEEF, [(5, 8, 0.5), (12, 10, 0.5), (9, 9, 0.0)]),
    ("rough", 12, 10,   1000,   0xC0DE, [(7, 8, 0.5)]),
    ("wild",  12, 10,   5000,   0x5EED, []),
    ("neg",   10,  8,   -50,    0x0F0F, []),                 # 负 Ruggedness：也走人
    ("many",  14, 11,   400,    0xABCD,
     [(x, 16 - x, 0.5) for x in range(6, 12)]),
]


def main():
    lines = []
    for name, w, h, rug, seed, anchors in CASES:
        o = HeightOracle(w, h)
        res = o.run(rug, seed, anchors)
        lines.append("CASE %s %d %d %d %d %d" % (name, w, h, rug, seed,
                                                 len(anchors)))
        for (x, y, hv) in anchors:
            lines.append("ANCHOR %d %d %s" % (x, y, struct.pack("<d", hv).hex()))
        lines.append("HEIGHT " + ",".join(struct.pack("<d", a).hex()
                                          for a, b in res))
        lines.append("SLOPE " + ",".join(struct.pack("<d", b).hex()
                                         for a, b in res))
    path = sys.argv[1] if len(sys.argv) > 1 else "height_field_golden.txt"
    with open(path, "w") as f:
        f.write("# 阶段 9 高度场 0x5A2F50 黄金基准（Unicorn 跑原版机器码）\n")
        f.write("# 由 code/randmap/dump_height_field_golden.py 生成\n")
        f.write("\n".join(lines) + "\n")
    print("%d cases -> %s" % (len(CASES), path))


if __name__ == "__main__":
    main()
