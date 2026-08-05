# -*- coding: utf-8 -*-
"""反编译 CD 校验函数：CCFileClass::CDCheck / RawFileClass::CDCheck
回答：原版 CD 校验失败时静默返回还是阻断流程（用户"无感"的解释）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_cdcheck.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "cdcheck_decomp.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

ANCHORS = [
    ("0x473ab0", "CCFileClass::CDCheck (ScenarioClass::Start 调用点,返回值被忽略)"),
    ("0x65ca70", "RawFileClass::CDCheck"),
]

for s, label in ANCHORS:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        f.write("NO FUNCTION containing %s (%s)\n" % (s, label))
        continue
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 120, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("CDCHECK_DECOMP_DONE")
