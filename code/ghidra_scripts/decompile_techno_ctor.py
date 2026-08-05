# -*- coding: utf-8 -*-
"""反编译 TechnoClass 构造函数全文，手动定位 vtable"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\techno_ctor_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in [("0x6f2b40", "TechnoClass::TechnoClass")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("## %s  %s\n" % (label, s))
    if fn is None:
        f.write("NO FUNCTION\n")
        continue
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())

f.close()
decomp.dispose()
print("CTOR_DECOMP_DONE")
