# -*- coding: utf-8 -*-
"""涂块的「投放策略」：三种 MapType 各自决定往哪里、撒几块、每块多大。

涂块核心 FUN_0059BBC0 已还原（见 docs/random-map/stage-seeding-water.md §三），
剩下的形状自由度全在这一层：区域四元组 (x,y,w,h) 从哪来、预算怎么定、
失败重试几次。这一层也决定了各 MapType 消耗多少随机数。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_blob_placement.txt")

TARGETS = [
    ("0x59a8f0", "区域四元组列表生成（疑似按玩家数分区）"),
    ("0x59ad10", "MapType 0：按点列表撒块"),
    ("0x59afa0", "MapType 1：按目标覆盖率生长"),
    ("0x59b200", "MapType 2"),
    ("0x59a6c0", "Seeding water 阶段调度"),
    ("0x42b1f0", "可用格子总数（覆盖率分母）"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface(); decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## 涂块投放策略反编译\n")
for sa, label in TARGETS:
    addr = af.getAddress(sa)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("\n" + "=" * 70 + "\n## %s @ %s\n\n" % (label, sa))
    if fn is None:
        f.write("// 无法建立函数\n"); continue
    res = decomp.decompileFunction(fn, 300, monitor)
    f.write(res.getDecompiledFunction().getC() if res.decompileCompleted()
            else "// 反编译失败: %s\n" % res.getErrorMessage())
f.close()
print("written: " + OUT)
