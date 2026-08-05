# -*- coding: utf-8 -*-
"""反编译 woldata.key 字符串引用点 0x5DC283 所在函数（woldata.key 读取逻辑）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_woldata2.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "woldata_read_fn.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 引用点所在函数 + 函数边界汇编
for ref_va in ["0x5dc283", "0x5dc296"]:
    addr = af.getAddress(ref_va)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n## ref %s  fn=%s\n" % (
        ref_va, fn.getName() if fn else "NONE"))
    if fn is None:
        # 汇编兜底
        a = addr.subtract(0x30)
        end = addr.add(0x60)
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
        continue
    res = decomp.decompileFunction(fn, 180, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("WOLDATA2_DONE")
