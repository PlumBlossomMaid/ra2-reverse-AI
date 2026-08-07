# -*- coding: utf-8 -*-
u"""导出原版 atan（0x4CADE0）的对拍基准。

它是**纯查表**：k = abs(_ftol(x / 0.024413354694843292f))，查
float atanTable[0x1001] @0x8610B4；越界给 float pi/2 @0x7E897C。
全程只有一次 fdiv、一次 fst dword 窄化和整数运算，不涉及 fpatan / fyl2x
那类超越指令，所以 Unicorn 在这里是可信的。

oracle 自检（README 里那条"改一个本该改变结果的输入"）已做过，结论分两层：

* 直接换 atan 的控制字 0x0E7F -> 0x027F，**399 个样本无一变化**——
  但这不是模拟器失灵，而是这个函数本身几乎与舍入模式无关：
  取下标的 `_ftol` @0x7C5F00 自己会 `fnstcw` 比对 [0x822D80]，
  不一致就走强制截断的慢路径，所以下标恒为截断；
  剩下的 `fdiv` 与 `fst dword` 只影响到"1 ulp 是否跨过整数边界"这种概率事件。
* 于是改用指令级探针单独验这两条：`fdiv` 在 1521 组 a/b 里有 546 组随控制字
  改变结果，`fst dword`（1/3 窄化成 float）给出 0x3EAAAAAA(chop) / 0x3EAAAAAB(near)。
  **两条都吃舍入模式，模拟器在此可信。**

输出写到 memory/data/rmg/atan_golden.inc，由 test_fast_math.cpp include。
"""
import io
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle

ADDR_ATAN = 0x4CADE0

SCRATCH = 0x40000000
RESULT = 0x40000800
MAGIC = 0x1F000000

STEP = 0.024413354694843292   # [0x8650B8]，float


class AtanOracle(object):
    def __init__(self):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_FPCW
        self.o = Oracle()
        self.o.uc.mem_map(SCRATCH, 0x1000)
        # fstp qword [RESULT]; push MAGIC; ret
        self.o.uc.mem_write(SCRATCH, b"\xDD\x1D" + struct.pack("<I", RESULT) +
                            b"\x68" + struct.pack("<I", MAGIC) + b"\xC3")
        self._ESP, self._FPCW = UC_X86_REG_ESP, UC_X86_REG_FPCW
        self.sp0 = 0x20000000 + 0x100000 - 0x3000

    def call(self, x):
        sp = self.sp0 - 8
        self.o.uc.mem_write(sp, struct.pack("<d", x))
        sp -= 4
        self.o.uc.mem_write(sp, struct.pack("<I", SCRATCH))
        self.o.uc.reg_write(self._ESP, sp)
        self.o.uc.reg_write(self._FPCW, 0x0E7F)
        self.o.uc.emu_start(ADDR_ATAN, MAGIC)
        return struct.unpack("<d", self.o.uc.mem_read(RESULT, 8))[0]


def bits(x):
    return struct.unpack("<Q", struct.pack("<d", x))[0]


def main():
    t = AtanOracle()
    xs = [0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0,
          1e-9, -1e-9, 1e-30, -1e-30]
    # 表下标 0 / 1 / 2 附近，验「向零截断」而不是就近
    for k in (0, 1, 2, 3, 100, 1000, 4095, 4096, 4097, 5000):
        for eps in (-1e-6, 0.0, 1e-6, 0.5):
            xs.append((k + eps) * STEP)
    # |x| ~ 100 处的跳变（table[4096] = 1.5607948 → float pi/2 = 1.5707964）
    xs += [99.0, 99.9, 99.99, 100.0, 100.05, 101.0, -100.0, -101.0,
           1e3, -1e3, 1e6, -1e6]
    # 角度扩张 0x5A08D0 的真实实参形态：-(dy)/dx，dx/dy 都是整数格坐标
    for dy in (-7, -3, -1, 0, 1, 3, 7, 40):
        for dx in (1, 2, 3, 5, 11, 64, 200):
            xs.append(-float(dy) / float(dx))
    # 去重但保持顺序（-0.0 与 0.0 在 dict 里会撞，所以按位形去重）
    seen, uniq = set(), []
    for x in xs:
        b = bits(x)
        if b in seen:
            continue
        seen.add(b)
        uniq.append(x)

    buf = io.StringIO()
    buf.write(u"// x, Atan(x) —— 两列都是 double 位形\n")
    buf.write(u"// 由 code/randmap/dump_atan_golden.py 用 Unicorn 跑原版 0x4CADE0 取得\n")
    buf.write(u"static const std::uint64_t kAtanGolden[][2] = {\n")
    for x in uniq:
        r = t.call(x)
        buf.write(u"    {0x%016Xull, 0x%016Xull},  // atan(% .9g) = % .9g\n"
                  % (bits(x), bits(r), x, r))
    buf.write(u"};\n")
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "memory", "data", "rmg", "atan_golden.inc")
    out = os.path.normpath(out)
    with io.open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(buf.getvalue())
    print("written: %s  (%d cases)" % (out, len(uniq)))


if __name__ == "__main__":
    main()
