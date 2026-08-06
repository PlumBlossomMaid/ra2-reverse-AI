# -*- coding: utf-8 -*-
"""Seeding water 阶段的核心实现与判定谓词

FUN_0059BBC0 是「涂一块地」的核心（三处取随机数），MapType 0/1/2 三种布局
都靠反复调用它把陆地从整张水面里抠出来。其余是布局用到的谓词与工具。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_water_core.txt")

TARGETS = [
    ("0x59bbc0", "涂块核心（3 处取随机数）"),
    ("0x59a8f0", "布局点列表生成（MapType 0 用）"),
    ("0x5ada40", "布局前置初始化"),
    ("0x42b1f0", "可用格子总数？（MapType 1 的分母）"),
    ("0x486380", "格子谓词 A（邻域判定）"),
    ("0x4865b0", "格子谓词 B"),
    ("0x57a0c0", "水域后处理"),
    ("0x578350", "MapClass 单元遍历: Reset"),
    ("0x578290", "MapClass 单元遍历: Next"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## Seeding water 核心实现反编译\n")
for sa, label in TARGETS:
    addr = af.getAddress(sa)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("\n" + "=" * 70 + "\n")
    f.write("## %s  @ %s  fn=%s\n\n" % (label, sa, fn.getName() if fn else "CREATE_FAILED"))
    if fn is None:
        continue
    res = decomp.decompileFunction(fn, 300, monitor)
    f.write(res.getDecompiledFunction().getC() if res.decompileCompleted()
            else "// 反编译失败: %s\n" % res.getErrorMessage())
f.close()
print("written: " + OUT)
