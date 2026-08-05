# -*- coding: utf-8 -*-
"""UnitClass::Save/Load 字段标注取证
从 UnitClass 构造函数找 vtable, 读 IPersistStream 槽位 (Load=0x14, Save=0x18),
反编译 Save/Load 函数
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_unitclass_save.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs
import re

OUT = r"E:\code\ra2-reverse\unitclass_save_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1. 反编译 UnitClass 构造函数找 vtable
addr = af.getAddress("0x7353c0")
fn = fm.getFunctionAt(addr)
res = decomp.decompileFunction(fn, 60, monitor)
c_code = res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAILED"
f.write("## UnitClass::UnitClass @ 0x7353C0 反编译候选 vtable\n")
candidates = set()
for m in re.finditer(r"0x[0-9a-fA-F]{6,8}", c_code):
    v = int(m.group(0), 16)
    if 0x7e0000 <= v <= 0x820000:
        candidates.add(v)
for v in sorted(candidates):
    f.write("  0x%08x\n" % v)

# 2. 对每个候选读 IPersistStream 槽位
f.write("\n## IPersistStream 槽位探测 (Load=0x14, Save=0x18, GetClassID=0xC)\n")
targets = []
for v in sorted(candidates):
    try:
        a = af.getAddress("0x%x" % v)
        row = []
        for slot in [0xc, 0x14, 0x18]:
            tgt = mem.getInt(a.add(slot), False)
            tfn = fm.getFunctionAt(af.getAddress("0x%08x" % tgt))
            name = tfn.getName() if tfn else "?"
            row.append("+0x%02x -> 0x%08x %s" % (slot, tgt, name))
        f.write("  vtable 0x%08x: %s\n" % (v, " | ".join(row)))
        targets.append((v, row))
    except Exception as e:
        f.write("  vtable 0x%08x: ERR %s\n" % (v, e))

# 3. 反编译 Save/Load 槽位函数
f.write("\n## 反编译 Save/Load\n")
for v, row in targets:
    for slot in [0x14, 0x18]:
        try:
            tgt = mem.getInt(af.getAddress("0x%x" % v).add(slot), False)
            fn2 = fm.getFunctionAt(af.getAddress("0x%08x" % tgt))
            if fn2 is None:
                f.write("vtable 0x%08x +0x%02x: 0x%08x NO FUNCTION\n" % (v, slot, tgt))
                continue
            f.write("=" * 70 + "\n## vtable 0x%08x +0x%02x -> %s @ %s\n" % (v, slot, fn2.getName(), tgt))
            res2 = decomp.decompileFunction(fn2, 60, monitor)
            if res2.decompileCompleted():
                f.write(res2.getDecompiledFunction().getC())
            else:
                f.write("DECOMPILE FAILED: %s\n" % res2.getErrorMessage())
            f.write("\n")
        except Exception as e:
            f.write("ERR: %s\n" % e)

f.close()
decomp.dispose()
print("UNITCLASS_SAVE_DECOMP_DONE")
