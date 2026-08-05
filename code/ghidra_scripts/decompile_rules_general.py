# -*- coding: utf-8 -*-
"""反编译 RulesClass::Read_General，定位 0x570-0x57c/0x758 对应字段"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\rules_general_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

addr = af.getAddress("0x66d530")
fn = fm.getFunctionAt(addr)
f.write("## RulesClass::Read_General  0x66d530\n")
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
print("RULESGEN_DONE")
