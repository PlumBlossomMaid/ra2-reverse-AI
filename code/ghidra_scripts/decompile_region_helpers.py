# -*- coding: utf-8 -*-
"""反编译 Init regions 用到的谓词/容器辅助函数。
输出：memory/data/decomp/rmg_region_helpers.txt
"""
import codecs
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

TARGETS = [
    (0x5A0700, u"MapSeedClass 方法：返回一组候选（含 +0x10 计数 / +0x04 数组）"),
    (0x4865D0, u"CellClass 谓词 A"),
    (0x4867B0, u"CellClass 谓词 B"),
    (0x5657A0, u"由 packed XY 取 CellClass*"),
    (0x42D510, u"坐标加偏移（AdjacentCell）"),
    (0x5AC370, u"区域计数用的判定"),
    (0x58BF70, u"region 对象构造"),
    (0x58D010, u"Init regions 收尾"),
]

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_region_helpers.txt")

di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

f = codecs.open(OUT, "w", "utf-8")
f.write(u"## Init regions 辅助函数反编译\n")
for va, note in TARGETS:
    fn = fm.getFunctionAt(af.getAddress("0x%x" % va))
    if fn is None:
        f.write(u"\n## 0x%06X  %s\n(Ghidra 未定义)\n" % (va, note))
        continue
    f.write(u"\n## 0x%06X  %s  fn=%s\n" % (va, note, fn.getName()))
    r = di.decompileFunction(fn, 120, mon)
    f.write(r.getDecompiledFunction().getC() if r.decompileCompleted() else u"(失败)\n")
f.close()
print("written: " + OUT)
