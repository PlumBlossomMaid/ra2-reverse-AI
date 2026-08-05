# -*- coding: utf-8 -*-
"""反编译 TimeToBuild 的两个浮点辅助函数"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

ADDRS = [
    ("0x7c5f00", "FUN_007c5f00 (TimeToBuild 里的浮点辅助)"),
    ("0x50c0a0", "FUN_0050c0a0 (TimeToBuild 里的调用)"),
]

OUT = r"E:\code\ra2-reverse\float_helpers_decomp.txt"
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
        cmd.applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
        if fn is None:
            f.write("NO FUNCTION at %s\n" % s)
            continue
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("FLOATHELP_DONE")
