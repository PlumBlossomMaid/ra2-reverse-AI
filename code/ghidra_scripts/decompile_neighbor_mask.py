# -*- coding: utf-8 -*-
"""FUN_0057B210(cell, mode)——岸线平滑用的八邻域地形位掩码

目标：确认 8 个 bit 各自对应哪个方向、mode 0 与 mode 2 判定谓词的差别。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_neighbor_mask.txt")

TARGETS = [
    ("0x57b210", "八邻域地形位掩码 mask(cell, mode)"),
    ("0x485060", "平滑第 3/4 遍用到的格子谓词"),
    ("0x5a00c0", "第 1 遍取「块编号」用"),
    ("0x5a0090", "第 1 遍写「块编号」用"),
    ("0x57b440", "贴砖落地"),
    ("0x486380", "mode 0 的额外门槛谓词"),
    ("0x58c2a0", "由 CellClass 取工作记录"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## 八邻域掩码及其邻居反编译\n")
for sa, label in TARGETS:
    addr = af.getAddress(sa)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("\n" + "=" * 70 + "\n")
    f.write("## %s  @ %s\n\n" % (label, sa))
    if fn is None:
        f.write("// CREATE_FAILED\n")
        continue
    res = decomp.decompileFunction(fn, 300, monitor)
    f.write(res.getDecompiledFunction().getC() if res.decompileCompleted()
            else "// 反编译失败: %s\n" % res.getErrorMessage())
f.close()
print("written: " + OUT)
