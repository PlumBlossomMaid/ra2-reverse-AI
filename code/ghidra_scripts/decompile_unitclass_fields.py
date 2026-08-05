# -*- coding: utf-8 -*-
"""UnitClass::Save/Load/GetClassID 字段标注取证
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_unitclass_fields.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x744600", "UnitClass::Save (vtable+0x18)"),
    ("0x744470", "UnitClass::Load (vtable+0x14)"),
    ("0x746de0", "UnitClass::GetClassID (vtable+0xC)"),
]

OUT = r"E:\code\ra2-reverse\unitclass_fields_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    if fn is None:
        f.write("NO FUNCTION\n")
        continue
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("UNITCLASS_FIELDS_DECOMP_DONE")
