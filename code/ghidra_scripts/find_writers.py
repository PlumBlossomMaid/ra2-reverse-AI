# -*- coding: utf-8 -*-
"""定位若干 .bss 全局的初始化点：谁往这些地址写过。

.bss 里的全局在文件里没有初值，必须找到运行期初始化代码才能还原其内容。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.symbol import RefType
import codecs, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_bss_writers.txt")

TARGETS = [
    ("0x89f688", "四/八邻接方向增量表"),
    ("0xabdfb8", "高斯随机数发生器全局对象"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
rm = currentProgram.getReferenceManager()
decomp = DecompInterface(); decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
seen = set()
for sa, label in TARGETS:
    addr = af.getAddress(sa)
    f.write("\n" + "=" * 70 + "\n## %s @ %s\n" % (label, sa))
    writers = []
    for r in rm.getReferencesTo(addr):
        if r.getReferenceType().isWrite():
            fn = fm.getFunctionContaining(r.getFromAddress())
            writers.append((r.getFromAddress(), fn))
    f.write("写引用 %d 处\n" % len(writers))
    for fromAddr, fn in writers[:20]:
        f.write("  from %s  in %s\n" % (fromAddr, fn.getName() if fn else "?"))
    for fromAddr, fn in writers[:6]:
        if fn is None or fn.getEntryPoint() in seen:
            continue
        seen.add(fn.getEntryPoint())
        res = decomp.decompileFunction(fn, 240, monitor)
        f.write("\n--- %s @ %s ---\n" % (fn.getName(), fn.getEntryPoint()))
        f.write(res.getDecompiledFunction().getC() if res.decompileCompleted() else "// fail\n")
f.close()
print("written: " + OUT)
