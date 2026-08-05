# -*- coding: utf-8 -*-
"""TechnoClass::Save (FUN_0070C250) 字段标注 —— Type 指针 +0x670 写入位置
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_techno_save_base.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\techno_save_base_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in [("0x70c250", "TechnoClass::Save (FUN_0070C250)")]:
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

f.close()
decomp.dispose()
print("TECHNO_SAVE_BASE_DONE")
