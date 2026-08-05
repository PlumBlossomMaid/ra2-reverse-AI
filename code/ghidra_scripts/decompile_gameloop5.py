# -*- coding: utf-8 -*-
"""第五轮：dump 0x4F8400-0x4F8A00 汇编（胜负判定区，Ghidra 未建函数）+ 补反编译
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_gameloop5.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "gameloop_decomp5.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1) dump 0x4F8400 - 0x4F8A00 汇编（每条指令一行）
f.write("=" * 70 + "\n## 汇编 [0x4f8400 - 0x4f8a00]（胜负判定写入点 0x4f867c/0x4f8692/0x4f86ee/0x4f87bb）\n")
start = af.getAddress("0x4f8400")
end = af.getAddress("0x4f8a00")
addr = start
while addr is not None and addr.compareTo(end) <= 0:
    ins = listing.getInstructionAt(addr)
    if ins is None:
        ins = listing.getInstructionAfter(addr)
        if ins is None or ins.getAddress().compareTo(end) > 0:
            break
        addr = ins.getAddress()
        continue
    f.write("%s  %s\n" % (ins.getAddress(), ins))
    addr = ins.getAddress().add(ins.getLength())
f.write("\n")

# 2) 反编译标志4 写入者 FUN_004c6cb0
for s, label in [("0x4c6cb0", "标志4(0xa83d48) 写入者 FUN_004c6cb0")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        fn = fm.getFunctionContaining(addr)
    if fn is None:
        f.write("NO FUNCTION at %s (%s)\n" % (s, label))
        continue
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

# 3) FUN_00686730 是否调用 ReadScenario_MissionINI(0x686b20)
f.write("=" * 70 + "\n## FUN_00686730 [0x686730 - 0x686b20) 内调用 0x686b20 的引用\n")
refmgr = currentProgram.getReferenceManager()
target = af.getAddress("0x686b20")
refs = refmgr.getReferencesTo(target)
for ref in refs:
    fromAddr = ref.getFromAddress()
    fn = fm.getFunctionContaining(fromAddr)
    f.write("  %s @ %s  %s\n" % (ref.getReferenceType(), fromAddr, fn.getName() if fn else "?"))
f.write("\n")

f.close()
decomp.dispose()
print("GAMELOOP5_DECOMP_DONE")
