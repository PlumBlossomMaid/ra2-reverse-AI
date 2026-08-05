# -*- coding: utf-8 -*-
"""查 woldata.key 字符串 (0x831398) 的所有引用点 + 引用函数反编译
回答：改 .key 一个字符为何还能玩——woldata.key 验签在哪条路径（WOL vs 单机）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_woldata_xref.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "woldata_xref.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

STR_VA = "0x831398"  # "woldata.key" 字符串（文件偏移 0x431398 映射）
TARGETS = [STR_VA, "0x8313a3", "0x8313a9"]  # woldata.key / Serial / SOFTWARE\Westwood\...

for s in TARGETS:
    addr = af.getAddress(s)
    f.write("=" * 70 + "\n## xrefs to %s\n" % s)
    refs = currentProgram.getReferenceManager().getReferencesTo(addr)
    for r in refs:
        f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
    f.write("\n")
    # 对每个引用点，找所在函数并反编译
    for r in refs:
        from_addr = r.getFromAddress()
        fn = fm.getFunctionContaining(from_addr)
        f.write("## ref from %s  fn=%s\n" % (
            from_addr, fn.getName() if fn else "NONE"))
        if fn is not None:
            res = decomp.decompileFunction(fn, 120, monitor)
            if res.decompileCompleted():
                f.write(res.getDecompiledFunction().getC())
            else:
                f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
        f.write("\n")

f.close()
decomp.dispose()
print("WOLDATA_XREF_DONE")
