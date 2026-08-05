# -*- coding: utf-8 -*-
"""第四轮：反编译胜负判定函数(0x4F86xx 区) + 标志4 写入者 FUN_004c6cb0 + 场景读取链确认
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_gameloop4.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "gameloop_decomp4.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1) dump 0x4F8500-0x4F8A00 函数列表（胜负判定函数边界）
f.write("=" * 70 + "\n## 函数列表 [0x4f8500 - 0x4f8a00]\n")
it = fm.getFunctions(af.getAddress("0x4f8500"), True)
for fn in it:
    addr = fn.getEntryPoint()
    if addr.compareTo(af.getAddress("0x4f8a00")) > 0:
        break
    f.write("%s\t%s\n" % (addr, fn.getName()))
f.write("\n")

# 2) 反编译胜负判定函数（含 0x4F867C 写入点）
for s, label in [("0x4f867c", "胜负判定 (写入 0xa83d49 胜利标志 / 0xa8ecd0 失败标志)")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n")
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

# 3) 标志4 写入者 FUN_004c6cb0
for s, label in [("0x4c6cb0", "标志4(0xa83d48) 写入者 FUN_004c6cb0")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n")
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

# 4) 场景读取链确认：FUN_00686730 是否调用 ReadScenario_MissionINI(0x686b20)
f.write("=" * 70 + "\n## 场景读取链 FUN_00686730 -> 引用 0x686b20?\n")
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
print("GAMELOOP4_DECOMP_DONE")
