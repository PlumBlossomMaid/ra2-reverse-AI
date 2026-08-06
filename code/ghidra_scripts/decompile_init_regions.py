# -*- coding: utf-8 -*-
"""反编译 RMG 阶段 3「Init regions」涉及的函数。

入口取自主流程 FUN_00598960 中两条调试日志之间的调用序列。
输出：memory/data/decomp/rmg_init_regions.txt
"""
import codecs
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

TARGETS = [
    (0x5AC290, u"区域对象清理/重置（循环里对每个 region 调）"),
    (0x58CF90, u"Init regions 主体 1"),
    (0x58E740, u"Init regions 主体 2（带 mode 参数 4/5/6）"),
    (0x58E9B0, u"Init regions 主体 3"),
    (0x58D010, u"Init regions 收尾"),
    (0x58C800, u"含唯一取数点 0x58CAF9"),
    (0x58C6F0, u"取数点 0x58C7B5 所在（阶段 4 用）"),
]

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_init_regions.txt")

di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

f = codecs.open(OUT, "w", "utf-8")
f.write(u"## RMG 阶段 3 Init regions 反编译\n\n")
for va, note in TARGETS:
    a = af.getAddress("0x%x" % va)
    fn = fm.getFunctionAt(a)
    if fn is None:
        f.write(u"\n## 0x%06X  %s\n(Ghidra 未定义该函数)\n" % (va, note))
        continue
    f.write(u"\n## 0x%06X  %s  fn=%s\n" % (va, note, fn.getName()))
    r = di.decompileFunction(fn, 120, mon)
    f.write(r.getDecompiledFunction().getC() if r.decompileCompleted() else u"(反编译失败)\n")
f.close()
print("written: " + OUT)
