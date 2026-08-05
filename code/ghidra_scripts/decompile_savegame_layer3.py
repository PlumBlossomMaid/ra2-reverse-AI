# -*- coding: utf-8 -*-
"""第三层取证: AbstractClass::Load/Save + TechnoClass 序列化 + SwizzleManager
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_savegame_layer3.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x4103c0", "AbstractClass::Load 区 (probe 0x4103D0 hook)"),
    ("0x410310", "AbstractClass::Save 区 (探测)"),
    ("0x6f2b40", "TechnoClass::TechnoClass (vtable 参考)"),
]

OUT = r"E:\code\ra2-reverse\savegame_layer3_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

# 1. 探测
f.write("=" * 70 + "\n## 探测\n")
for s, label in ADDRS:
    fn = fm.getFunctionContaining(af.getAddress(s))
    if fn is None:
        f.write("%s (%s): NOT IN ANY FUNCTION\n" % (s, label))
    else:
        body = fn.getBody()
        f.write("%s (%s): %s @ %s [%s - %s]\n" % (s, label, fn.getName(), fn.getEntryPoint(),
                                                   body.getMinAddress(), body.getMaxAddress()))
f.write("\n")

decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

done = set()
for s, label in ADDRS:
    fn = fm.getFunctionContaining(af.getAddress(s))
    if fn is None:
        continue
    entry = str(fn.getEntryPoint())
    if entry in done:
        continue
    done.add(entry)
    f.write("=" * 70 + "\n## %s  (containing %s)\n" % (fn.getName(), s))
    res = decomp.decompileFunction(fn, 90, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SAVEGAME_LAYER3_DECOMP_DONE")
