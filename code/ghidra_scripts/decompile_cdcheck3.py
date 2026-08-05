# -*- coding: utf-8 -*-
"""反编译 0x4790E0（0x7E4C30 函数指针指向的前置检查）+ 0x473AB0 未命名函数
回答：原版 CD/正版门禁在哪，是否被 patch
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_cdcheck3.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "cdcheck3_decomp.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

def decompile_at(s, label):
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n## %s @ %s  containing-fn=%s\n" % (
        label, s, fn.getName() if fn else "NONE"))
    if fn is None:
        # 汇编 dump 兜底
        start = addr
        end = addr.add(0x80)
        a = start
        while a is not None and a.compareTo(end) < 0:
            inst = listing.getInstructionAt(a)
            if inst is None:
                inst = listing.getDataAt(a)
            if inst is not None:
                f.write("%s\t%s\n" % (a, inst))
                a = a.add(inst.getLength())
            else:
                f.write("%s\t???\n" % a)
                a = a.add(1)
        f.write("\n")
        return
    res = decomp.decompileFunction(fn, 120, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

decompile_at("0x4790e0", "0x7E4C30 函数指针目标（ScenarioClass::Start 门禁）")
decompile_at("0x473ab0", "0x473AB0 未命名（CDFileClass 区）")

# xref：谁写 0x7E4C30 / 谁调用 0x4790E0
f.write("=" * 70 + "\n## xrefs to 0x4790E0 (callers)\n")
try:
    refs = currentProgram.getReferenceManager().getReferencesTo(af.getAddress("0x4790e0"))
    for r in refs:
        f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
except Exception as e:
    f.write("  xref error: %s\n" % e)
f.write("\n## xrefs to 0x7E4C30 (read/write of ptr)\n")
try:
    refs = currentProgram.getReferenceManager().getReferencesTo(af.getAddress("0x7e4c30"))
    for r in refs:
        f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
except Exception as e:
    f.write("  xref error: %s\n" % e)

f.close()
decomp.dispose()
print("CDCHECK3_DONE")
