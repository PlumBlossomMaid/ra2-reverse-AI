# -*- coding: utf-8 -*-
"""反编译 RulesClass::RulesClass 构造函数，提取威胁系数默认值"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\rules_ctor_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

addr = af.getAddress("0x665650")
fn = fm.getFunctionAt(addr)
f.write("## RulesClass::RulesClass  0x665650\n")
if fn:
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
else:
    f.write("NO FUNCTION\n")

f.close()
decomp.dispose()
print("RULESCTOR_DONE")
