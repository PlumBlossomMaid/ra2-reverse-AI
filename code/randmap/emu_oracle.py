# -*- coding: utf-8 -*-
"""原版函数模拟执行 oracle —— 用 Unicorn 在指令级跑 gamemd.exe 的机器码取"标准答案"

为什么用模拟而不是进程内直接调用：
  gamemd.exe 的 ImageBase 是 0x400000 且代码含绝对地址（如常量表 0x839644），
  必须映射到原址才能运行。但在 64 位 Windows 上，宿主进程的堆在加载器阶段
  就已铺满 0x400000-0x6C0000（实测 VirtualAlloc 返回 ERROR_INVALID_ADDRESS 487，
  连 /ENTRY 抢跑也来不及）。指令级模拟绕开这一切，且完全是对文件的只读分析。

用途：为 code/rewrite/rmg/ 的独立重写提供逐数值对拍基准。
"""
import struct

DEFAULT_EXE = r"E:\YRLauncher\gamemd.exe"

IMAGE_BASE = 0x400000
STACK_BASE = 0x20000000
STACK_SIZE = 0x100000
HEAP_BASE = 0x30000000
HEAP_SIZE = 0x10000
MAGIC_RET = 0x1F000000       # 哨兵返回地址：EIP 到这里就停机

ADDR_CTOR = 0x65C6D0         # Randomizer::Randomizer(uint seed)     __thiscall RET 4
ADDR_RANDOM = 0x65C780       # Randomizer::Random()                  __thiscall RET 0
ADDR_RANGED = 0x65C7E0       # Randomizer::RandomRanged(int,int)     __thiscall RET 8

OBJ_SIZE = 0x3F4             # +0 disabled, +4 index1, +8 index2, +0xC state[250]


def load_sections(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    image_size = struct.unpack_from("<I", data, pe + 24 + 56)[0]
    secoff = pe + 24 + optsz
    secs = []
    for i in range(nsec):
        o = secoff + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, roff, rsize))
    return data, base, image_size, secs


class Oracle(object):
    def __init__(self, exe_path=DEFAULT_EXE):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
        from unicorn.x86_const import UC_X86_REG_ESP

        data, base, image_size, secs = load_sections(exe_path)
        assert base == IMAGE_BASE, "ImageBase 非 0x400000: 0x%x" % base

        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        span = (image_size + 0xFFF) & ~0xFFF
        self.uc.mem_map(IMAGE_BASE, span)
        for name, vaddr, vsize, roff, rsize in secs:
            if rsize:
                self.uc.mem_write(IMAGE_BASE + vaddr, data[roff:roff + rsize])

        self.uc.mem_map(STACK_BASE, STACK_SIZE)
        self.uc.mem_map(HEAP_BASE, HEAP_SIZE)
        self.uc.mem_map(MAGIC_RET & ~0xFFF, 0x1000)

        self.obj = HEAP_BASE
        self._ESP = UC_X86_REG_ESP

    def _call(self, addr, ecx, stack_args=()):
        """__thiscall 调用：ECX = this，参数从右往左压栈，哨兵地址作返回地址"""
        from unicorn.x86_const import (UC_X86_REG_ECX, UC_X86_REG_EAX,
                                       UC_X86_REG_ESP)
        sp = STACK_BASE + STACK_SIZE - 0x1000
        for v in reversed(stack_args):
            sp -= 4
            self.uc.mem_write(sp, struct.pack("<I", v & 0xFFFFFFFF))
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", MAGIC_RET))
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, ecx)
        self.uc.emu_start(addr, MAGIC_RET)
        return self.uc.reg_read(UC_X86_REG_EAX)

    # --- 三个原版函数 ---
    def seed(self, s):
        self.uc.mem_write(self.obj, b"\xCC" * OBJ_SIZE)   # 先填毒，确认构造函数写全
        self._call(ADDR_CTOR, self.obj, (s & 0xFFFFFFFF,))

    def random(self):
        return self._call(ADDR_RANDOM, self.obj) & 0xFFFFFFFF

    def ranged(self, a, b):
        v = self._call(ADDR_RANGED, self.obj, (a, b))
        return struct.unpack("<i", struct.pack("<I", v & 0xFFFFFFFF))[0]

    # --- 对象状态 ---
    def dump_obj(self):
        raw = self.uc.mem_read(self.obj, OBJ_SIZE)
        disabled = raw[0]
        index1, index2 = struct.unpack_from("<ii", bytes(raw), 4)
        state = list(struct.unpack_from("<250I", bytes(raw), 0xC))
        return disabled, index1, index2, state


def main():
    import os
    import sys
    args = sys.argv[1:]
    exe = DEFAULT_EXE
    if args and os.path.isfile(args[0]):
        exe = args[0]
        args = args[1:]
    seeds = [int(x, 0) for x in args] or [0, 1, 11584, 60556, 0xDEADBEEF]

    o = Oracle(exe)
    for s in seeds:
        o.seed(s)
        disabled, i1, i2, state = o.dump_obj()
        print("[seed %u]" % (s & 0xFFFFFFFF))
        print("  disabled=%d index1=%d index2=%d" % (disabled, i1, i2))
        print("  state[0..7]   = " + " ".join("%08X" % v for v in state[:8]))
        print("  state[242..249]= " + " ".join("%08X" % v for v in state[242:250]))
        print("  random[0..15] = " + " ".join("%08X" % o.random() for _ in range(16)))
        o.seed(s)
        print("  ranged(0,99)  = " + " ".join("%d" % o.ranged(0, 99) for _ in range(16)))
        o.seed(s)
        print("  ranged(-5,5)  = " + " ".join("%d" % o.ranged(-5, 5) for _ in range(16)))
        o.seed(s)
        print("  ranged(1,1000)= " + " ".join("%d" % o.ranged(1, 1000) for _ in range(16)))


if __name__ == "__main__":
    main()
