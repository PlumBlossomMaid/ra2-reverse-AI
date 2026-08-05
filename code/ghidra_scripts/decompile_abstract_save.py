# -*- coding: utf-8 -*-
"""FUN_00410320 —— AbstractClass 层保存核心 (swizzle ID 写入确认)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_abstract_save.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\abstract_save_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in [("0x410320", "FUN_00410320 (AbstractClass 层 Save)"),
                 ("0x4103cb", "AbstractClass::Load (FUN_00410380 校对)")]:
    fn = fm.getFunctionContaining(af.getAddress(s))
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
print("ABSTRACT_SAVE_DONE")
