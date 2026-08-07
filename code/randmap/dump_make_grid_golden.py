# -*- coding: utf-8 -*-
"""区域网格 0x59A8F0 的整函数模拟执行 —— 取黄金基准。

MapType 0 用它把可用区域切成 n 个矩形投放区，直接决定大陆的分布骨架。
消耗随机数 1 + rem 个（rem = rows*cols - n），账本上不能漏。

伪造的运行环境很小：只需要 Randomizer [0xABE890] 和一个预分配好的
DynamicVectorClass 收结果（把 VectorMax 开大，Resize 的 vtable 就不会被调到）。
"""
import struct

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x400000
STACK_BASE = 0x20000000
STACK_SIZE = 0x100000

A_MAKEGRID = 0x59A8F0
A_RNGCTOR = 0x65C6D0
A_RANDOM = 0x65C780
A_MALLOC = 0x7C8E17
A_FREE = 0x7C8B3D
VEC_CAP = 4096


class GridOracle(object):
    def __init__(self, seed):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
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
        self.brk = SCRATCH
        self.rng_calls = 0
        self._call(A_RNGCTOR, 0xABE890, (seed,))
        uc.hook_add(UC_HOOK_CODE, self._h_malloc, begin=A_MALLOC, end=A_MALLOC)
        uc.hook_add(UC_HOOK_CODE, self._h_free, begin=A_FREE, end=A_FREE)
        uc.hook_add(UC_HOOK_CODE, self._h_rng, begin=A_RANDOM, end=A_RANDOM)

    def _bump(self, n):
        p = self.brk
        self.brk = (self.brk + n + 15) & ~15
        assert self.brk < SCRATCH + SCRATCH_SIZE, "scratch exhausted"
        self.uc.mem_write(p, b"\0" * n)
        return p

    def wr32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def rd32(self, a):
        return struct.unpack("<i", self.uc.mem_read(a, 4))[0]

    def _ret(self, uc, pop):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, struct.unpack("<I", uc.mem_read(esp, 4))[0])
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = struct.unpack("<I", uc.mem_read(uc.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
        uc.reg_write(UC_X86_REG_EAX, self._bump(n))
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    def _h_rng(self, uc, address, size, ud):
        self.rng_calls += 1

    def _call(self, addr, ecx, args=()):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX
        sp = STACK_BASE + STACK_SIZE - 0x4000
        for v in reversed(args):
            sp -= 4
            self.wr32(sp, v)
        sp -= 4
        self.wr32(sp, MAGIC_RET)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, ecx)
        self.uc.emu_start(addr, MAGIC_RET)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def make_grid(self, n, bounds):
        # DynamicVectorClass：vptr@0 Vector@4 VectorMax@8 IsValid@0xC
        # IsAllocated@0xD ActiveCount@0x10 GrowthStep@0x14
        buf = self._bump(VEC_CAP * 16)
        vec = self._bump(0x18)
        self.wr32(vec + 0x00, 0x7E4E78)      # 借用现成的 vtable，反正用不到
        self.wr32(vec + 0x04, buf)
        self.wr32(vec + 0x08, VEC_CAP)       # 容量开大 -> 不会触发 Resize
        self.uc.mem_write(vec + 0x0C, bytes([1, 0]))
        self.wr32(vec + 0x10, 0)
        self.wr32(vec + 0x14, 10)
        pb = self._bump(16)
        for i, v in enumerate(bounds):
            self.wr32(pb + i * 4, v)
        self.rng_calls = 0
        self._call(A_MAKEGRID, 0, (vec, n, pb))
        cnt = self.rd32(vec + 0x10)
        return [tuple(self.rd32(buf + i * 16 + k * 4) for k in range(4))
                for i in range(cnt)], self.rng_calls


CASES = [
    (0x12345678, 4, (0, 0, 70, 70)),
    (0x12345678, 5, (0, 0, 70, 70)),
    (0x0BADF00D, 6, (0, 0, 70, 70)),
    (0x0BADF00D, 7, (2, 3, 80, 60)),
    (0xDEADBEEF, 8, (2, 3, 80, 60)),
    (0xDEADBEEF, 9, (0, 0, 120, 120)),
    (0x5A5A5A5A, 10, (0, 0, 120, 120)),
    (0x5A5A5A5A, 11, (5, 5, 64, 48)),
    (0x00C0FFEE, 12, (5, 5, 64, 48)),
    (0x00000001, 2, (0, 0, 70, 70)),
    (0x00000001, 3, (0, 0, 70, 70)),
    (0xFFFFFFFF, 1, (0, 0, 70, 70)),
    (0x13579BDF, 12, (0, 0, 70, 70)),
    (0x2468ACE0, 9, (1, 1, 33, 97)),
    (0x0F0F0F0F, 16, (0, 0, 100, 100)),
    (0x77777777, 25, (0, 0, 100, 100)),
]


def main():
    out = []
    for seed, n, bounds in CASES:
        o = GridOracle(seed)
        rects, calls = o.make_grid(n, bounds)
        out.append("CASE 0x%08X %d %d,%d,%d,%d -> cnt=%d rng=%d"
                   % (seed, n, bounds[0], bounds[1], bounds[2], bounds[3],
                      len(rects), calls))
        out.append("RECTS " + " ".join("%d,%d,%d,%d" % r for r in rects))
    path = r"E:\code\ra2-reverse-AI\memory\data\rmg\make_grid_golden.txt"
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("\n".join(out[:8]))
    print("wrote %s" % path)


if __name__ == "__main__":
    main()
