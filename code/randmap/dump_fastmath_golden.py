# -*- coding: utf-8 -*-
"""导出原版数学基元的对拍基准：查表近似 sqrt + 正态分布发生器

两者都是地图生成的形状来源，且都不能用标准库函数替代：
  0x4CAC40  近似 sqrt —— 16384 项查表，只有 14 位精度，与 std::sqrt 系统性不等
  0x5980C0  正态分布 —— Marsaglia 极坐标法，内部用 x87 fyl2x 求 ln，并缓存第二个值
  0x598000  均匀源   —— (double)(uint32)Random() * (1/4294967295.0)

注意：0x5980C0 内部的 `fyl2x` 在 Unicorn 上是**不可信**的（见 emu_oracle.py 顶部说明）。
本脚本导出的正态分布基准之所以仍然有效，是因为那 1 ulp 的 ln 差异被下游 FastSqrt
的 14 位查表完全吸收——这一点已由真 CPU 侧面印证：Unicorn(≈near53) / 真 CPU(fyl2x) /
复刻(chop53) 三种不同的 ln，经过 FastSqrt 之后产出同一批位形。
换句话说，这份基准是"碰巧仍然正确"，不是"取法可靠"。
若将来某个阶段直接消费 ln 的原值，必须换成真 CPU 探针取基准。
"""
import io
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "memory", "data", "rmg", "fastmath_golden.txt")

ADDR_FAST_SQRT = 0x4CAC40
ADDR_UNIFORM = 0x598000
ADDR_NORMAL = 0x5980C0
GAUSS_OBJ = 0xABDFB8      # {char have; double cached@8; funcptr uniform@0x10}

SCRATCH = 0x40000000
RESULT = 0x40000800
MAGIC = 0x1F000000


class FastMathOracle(object):
    def __init__(self):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_FPCW
        self.o = Oracle()
        self.o.uc.mem_map(SCRATCH, 0x1000)
        # 返回桩：把 x87 栈顶存成 double 再跳哨兵
        self.o.uc.mem_write(SCRATCH, b"\xDD\x1D" + struct.pack("<I", RESULT) +
                            b"\x68" + struct.pack("<I", MAGIC) + b"\xC3")
        self._ESP, self._ECX, self._FPCW = UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_FPCW
        self.sp0 = 0x20000000 + 0x100000 - 0x3000

    def _run(self, addr, ecx=0, dbl_arg=None):
        sp = self.sp0
        if dbl_arg is not None:
            sp -= 8
            self.o.uc.mem_write(sp, struct.pack("<d", dbl_arg))
        sp -= 4
        self.o.uc.mem_write(sp, struct.pack("<I", SCRATCH))
        self.o.uc.reg_write(self._ESP, sp)
        self.o.uc.reg_write(self._ECX, ecx)
        self.o.uc.reg_write(self._FPCW, 0x0E7F)
        self.o.uc.emu_start(addr, MAGIC)
        return struct.unpack("<d", self.o.uc.mem_read(RESULT, 8))[0]

    def fast_sqrt(self, x):
        return self._run(ADDR_FAST_SQRT, dbl_arg=x)

    def init_gauss(self):
        """复刻 0x598960 里对高斯对象的初始化：清标志 + 均匀源函数指针"""
        blob = struct.pack("<IIddI", 0, 0, 0.0, 0.0, ADDR_UNIFORM)[:0]  # 占位
        self.o.uc.mem_write(GAUSS_OBJ, b"\x00" * 0x10 + struct.pack("<I", ADDR_UNIFORM))

    def uniform(self):
        return self._run(ADDR_UNIFORM)

    def normal(self):
        return self._run(ADDR_NORMAL, ecx=GAUSS_OBJ)


def main():
    buf = io.StringIO()
    w = buf.write
    fm = FastMathOracle()

    w(u"# 原版数学基元对拍基准（Unicorn 模拟原版机器码，FPCW=0x0E7F）\n")

    w(u"\n## FastSqrt @0x4CAC40 —— 查表近似，非 std::sqrt\n")
    cases = [0.0, 1.0, 2.0, 3.0, 4.0, 9.0, 100.0, 0.25, 1e-6, 1e12,
             1234.5678, 7.0, 16777216.0, 1e-30, 123456789.0, 2.0000001]
    cases += [float(i) for i in range(1, 65)]
    for x in cases:
        w(u"sqrt %.17g -> %.17g\n" % (x, fm.fast_sqrt(x)))

    w(u"\n## Uniform @0x598000 / Normal @0x5980C0（全局 Randomizer 0xABE890）\n")
    for seed in (0, 1, 11584, 60556, 0xDEADBEEF):
        fm.o.seed_global(seed)
        fm.init_gauss()
        w(u"seed %u uniform: %s\n" % (seed & 0xFFFFFFFF,
                                      " ".join("%.17g" % fm.uniform() for _ in range(8))))
        fm.o.seed_global(seed)
        fm.init_gauss()
        w(u"seed %u normal : %s\n" % (seed & 0xFFFFFFFF,
                                      " ".join("%.17g" % fm.normal() for _ in range(16))))

    text = buf.getvalue()
    with io.open(os.path.normpath(OUT), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("written: " + os.path.normpath(OUT))


if __name__ == "__main__":
    main()
