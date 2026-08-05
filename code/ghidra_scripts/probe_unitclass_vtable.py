# -*- coding: utf-8 -*-
"""UnitClass vtable 0x7F5C70 的 IPersistStream 槽位 + Save/Load 反编译
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript probe_unitclass_vtable.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = r"E:\code\ra2-reverse\unitclass_save_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

VTABLES = ["0x7f5c70", "0x7f5c54"]

# 1. 槽位探测
f.write("## IPersistStream 槽位探测 (GetClassID=0xC, Load=0x14, Save=0x18, IsDirty=0x10)\n")
slots = {}
for v in VTABLES:
    try:
        a = af.getAddress(v)
        row = []
        for slot in [0x0, 0x4, 0x8, 0xc, 0x10, 0x14, 0x18, 0x1c]:
            tgt = mem.getInt(a.add(slot), False)
            tfn = fm.getFunctionAt(af.getAddress("0x%08x" % tgt))
            name = tfn.getName() if tfn else "?"
            row.append("+0x%02x:0x%08x(%s)" % (slot, tgt, name))
            if slot in (0x14, 0x18):
                slots[slot] = tgt
        f.write("vtable %s: %s\n" % (v, "  ".join(row)))
    except Exception as e:
        f.write("ERR %s: %s\n" % (v, e))

# 2. 反编译 Load(0x14)/Save(0x18)
f.write("\n")
for slot in [0x14, 0x18]:
    tgt = slots.get(slot)
    if not tgt:
        continue
    fn = fm.getFunctionAt(af.getAddress("0x%08x" % tgt))
    f.write("=" * 70 + "\n## vtable +0x%02x -> %s @ 0x%08x\n" % (slot, fn.getName() if fn else "?", tgt))
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
print("UNITCLASS_VTABLE_DONE")
