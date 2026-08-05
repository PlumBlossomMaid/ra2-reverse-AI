# -*- coding: utf-8 -*-
"""反编译真正检查函数 FUN_004A8270 + 查全局 0x89E3A0 写入点 + 调用点汇编
回答：原版 CD 门禁逻辑 + 破解/免检标志
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_cdcheck4.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "cdcheck4_decomp.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1) FUN_004A8270 反编译
addr = af.getAddress("0x4a8270")
fn = fm.getFunctionContaining(addr)
f.write("=" * 70 + "\n## FUN_004A8270 @ 0x4a8270 containing-fn=%s\n" % (fn.getName() if fn else "NONE"))
if fn is not None:
    res = decomp.decompileFunction(fn, 120, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
f.write("\n")

# 2) 全局 0x89E3A0 的引用
f.write("=" * 70 + "\n## xrefs to 0x89E3A0 (DAT_0089e3a0)\n")
refs = currentProgram.getReferenceManager().getReferencesTo(af.getAddress("0x89e3a0"))
for r in refs:
    f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
f.write("\n")

# 3) ScenarioClass::Start 里 0x683BEE 调用点上下文汇编
f.write("=" * 70 + "\n## asm around 0x683be6 (ScenarioClass::Start 门禁调用)\n")
start = af.getAddress("0x683bc0")
end = af.getAddress("0x683c30")
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

f.close()
decomp.dispose()
print("CDCHECK4_DONE")
