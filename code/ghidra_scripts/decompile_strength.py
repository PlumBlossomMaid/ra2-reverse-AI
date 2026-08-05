# -*- coding: utf-8 -*-
"""反编译 FUN_005f5c60 (目标强度) + 确认 WeaponTypeClass+0xa0 伤害表结构"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

OUT = r"E:\code\ra2-reverse\strength_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in [("0x5f5c60", "FUN_005f5c60 (ThreatCoefficients 目标强度)"),
                 ("0x4cac40", "FUN_004cac40 (平方和/sqrt)")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        cmd = CreateFunctionCmd(addr)
        cmd.applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    if fn:
        res = decomp.decompileFunction(fn, 60, monitor)
        if res.decompileCompleted():
            f.write(res.getDecompiledFunction().getC())
        else:
            f.write("DECOMPILE FAILED\n")
    else:
        f.write("NO FUNCTION\n")
    f.write("\n")

f.close()
decomp.dispose()
print("STRENGTH_DECOMP_DONE")
