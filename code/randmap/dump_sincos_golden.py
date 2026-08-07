# -*- coding: utf-8 -*-
u"""导出原版 sin/cos（0x4CACB0 / 0x4CAD00）的对拍基准。

两者都是**纯查表**：idx = _ftol(x * 8192/pi)，折半取表下标，再查
float sinTable[0x2800] @0x84F084。全程只有一次 fmul 和整数运算，
所以 Unicorn 在这里是可信的（不涉及 fyl2x / fsin 那类超越指令）。

输出直接是 C++ 源片段，贴进 test_fast_math.cpp。
"""
import io
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle

ADDR_SIN = 0x4CACB0
ADDR_COS = 0x4CAD00

SCRATCH = 0x40000000
RESULT = 0x40000800
MAGIC = 0x1F000000


class TrigOracle(object):
    def __init__(self):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_FPCW
        self.o = Oracle()
        self.o.uc.mem_map(SCRATCH, 0x1000)
        self.o.uc.mem_write(SCRATCH, b"\xDD\x1D" + struct.pack("<I", RESULT) +
                            b"\x68" + struct.pack("<I", MAGIC) + b"\xC3")
        self._ESP, self._FPCW = UC_X86_REG_ESP, UC_X86_REG_FPCW
        self.sp0 = 0x20000000 + 0x100000 - 0x3000

    def call(self, addr, x):
        sp = self.sp0 - 8
        self.o.uc.mem_write(sp, struct.pack("<d", x))
        sp -= 4
        self.o.uc.mem_write(sp, struct.pack("<I", SCRATCH))
        self.o.uc.reg_write(self._ESP, sp)
        self.o.uc.reg_write(self._FPCW, 0x0E7F)
        self.o.uc.emu_start(addr, MAGIC)
        return struct.unpack("<d", self.o.uc.mem_read(RESULT, 8))[0]


def bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def main():
    t = TrigOracle()
    xs = []
    for k in range(-8, 25):
        xs.append(k * math.pi / 8.0)
    xs += [0.0, -0.0, 1e-9, -1e-9, 0.5, -0.5, 1.0, -1.0,
           math.pi / 6, math.pi / 4, math.pi / 3, math.pi / 2,
           math.pi, 2 * math.pi, -math.pi, -2 * math.pi,
           3.0, 30.0, 300.0, -300.0, 1234.5, -1234.5,
           0.005, 0.0001, 6.2831, 6.2832]
    # 河流生成器实际会用到的角度：{pi/4, 3pi/4, 5pi/4, 7pi/4} 附近的游走
    for k in (1, 3, 5, 7):
        base = k * math.pi / 4
        for d in (-0.31415926535897931, -0.1, 0.0, 0.1, 0.31415926535897931):
            xs.append(base + d)

    w = io.StringIO().write
    buf = io.StringIO()
    buf.write(u"// x, sin(x), cos(x) —— 三列都是 double 位形\n")
    buf.write(u"static const std::uint64_t kTrigGolden[][3] = {\n")
    for x in xs:
        s = t.call(ADDR_SIN, x)
        c = t.call(ADDR_COS, x)
        buf.write(u"    {0x%016Xull, 0x%016Xull, 0x%016Xull},  // % .6f\n"
                  % (bits(x), bits(s), bits(c), x))
    buf.write(u"};\n")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "memory", "data", "rmg", "sincos_golden.inc")
    out = os.path.normpath(out)
    with io.open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(buf.getvalue())
    print("written: " + out)


if __name__ == "__main__":
    main()
