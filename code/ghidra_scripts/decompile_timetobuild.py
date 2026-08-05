# -*- coding: utf-8 -*-
"""补充反编译：GetCostPerStep(强制建函数) + TechnoClass::TimeToBuild"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

ADDRS = [
    ("0x4ca180", "FactoryClass::GetCostPerStep"),
    ("0x6f47a0", "TechnoClass::TimeToBuild"),
]

OUT = r"E:\code\ra2-reverse\time_to_build_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        cmd = CreateFunctionCmd(addr)
        if cmd.applyTo(currentProgram, monitor):
            fn = fm.getFunctionAt(addr)
            f.write("[created function at %s]\n" % s)
        else:
            f.write("FAILED to create function at %s (%s)\n" % (s, label))
            continue
    f.write("=" * 70 + "\n")
    f.write("## %s  %s\n" % (label, s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("TIMETOBUILD_DECOMP_DONE")
