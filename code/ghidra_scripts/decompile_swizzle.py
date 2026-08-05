# -*- coding: utf-8 -*-
"""SwizzleManagerClass::Register/Process 机制确认
FUN_006CF2C0 (加载时注册) / FUN_006CF230 (初始化/Process)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_swizzle.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x6cf2c0", "SwizzleManagerClass::Register (FUN_006CF2C0)"),
    ("0x6cf230", "SwizzleManagerClass::Process (FUN_006CF230)"),
]

OUT = r"E:\code\ra2-reverse\swizzle_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    fn = fm.getFunctionAt(af.getAddress(s))
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    if fn is None:
        f.write("NO FUNCTION\n")
        continue
    res = decomp.decompileFunction(fn, 90, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SWIZZLE_DONE")
