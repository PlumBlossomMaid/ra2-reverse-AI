# -*- coding: utf-8 -*-
"""定位 CCFileClass::CDCheck 真实函数体：dump 汇编 0x4738F0-0x473C00 + 尝试反编译候选入口
背景: yrpp_symbols.tsv 给 0x473ab0, named_functions.txt 给 0x4739f0, Ghidra 未建函数
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_cdcheck2.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "cdcheck2_decomp.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1) 该区间已识别的函数
f.write("## functions in [0x473800 - 0x473d00]\n")
it = fm.getFunctions(af.getAddress("0x473800"), True)
for fn in it:
    addr = fn.getEntryPoint()
    if addr.compareTo(af.getAddress("0x473d00")) > 0:
        break
    f.write("%s\t%s\n" % (addr, fn.getName()))
f.write("\n")

# 2) 汇编 dump
f.write("## asm [0x4738f0 - 0x473c00]\n")
addr = af.getAddress("0x4738f0")
end = af.getAddress("0x473c00")
while addr is not None and addr.compareTo(end) < 0:
    inst = listing.getInstructionAt(addr)
    if inst is None:
        inst = listing.getDataAt(addr)
    if inst is not None:
        f.write("%s\t%s\n" % (addr, inst))
        addr = addr.add(inst.getLength())
    else:
        f.write("%s\t???\n" % addr)
        addr = addr.add(1)
f.write("\n")

# 3) 尝试反编译候选入口
for cand in ["0x4739f0", "0x473ab0"]:
    a = af.getAddress(cand)
    fn = fm.getFunctionContaining(a)
    f.write("## candidate %s containing-fn=%s\n" % (cand, fn.getName() if fn else "NONE"))
    if fn is not None:
        res = decomp.decompileFunction(fn, 120, monitor)
        if res.decompileCompleted():
            f.write(res.getDecompiledFunction().getC())
        else:
            f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("CDCHECK2_DONE")
