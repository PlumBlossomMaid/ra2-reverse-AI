# -*- coding: utf-8 -*-
"""岸线平滑 FUN_0057A0C0 的四遍处理——决定一块陆地是被接受还是被抹回海里。

这是 Seeding water 阶段最后的缺口。它同时关系到两件事：
  1. 地形正确性：四遍平滑负责铺水陆过渡砖，任一遍失败整块陆地回滚成水；
  2. 随机数对齐：若这四遍内部取随机数，整条随机数流的账就对不上。
顺带取工作数组的存取器与「可用格子总数」的口径。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_shoreline.txt")

TARGETS = [
    ("0x57a430", "平滑第 1 遍"),
    ("0x57a320", "平滑第 2 遍"),
    ("0x57acf0", "平滑第 3/4 遍（参数 1 / 2）"),
    ("0x578290", "地图格子迭代器 Next"),
    ("0x578350", "地图格子迭代器 Reset"),
    ("0x578e60", "工作数组分配（side^2 x 0x50）"),
    ("0x58bdc0", "工作记录构造"),
    ("0x58c2c0", "工作记录按序号取"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface(); decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## 岸线平滑与工作数组反编译\n")
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
