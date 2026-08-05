# -*- coding: utf-8 -*-
"""地图生成器可行性侦察：随机地图入口 + 随机数发生器 + 地图读取
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_randmap.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "randmap_probe.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

ANCHORS = [
    ("0x597a10", "随机地图入口 FUN_00597A10 (加载层 0x684620 调用)"),
    ("0x65c7d0", "Random2Class::Random (Phobos: Random2Class_Random_SyncLog)"),
    ("0x65c88a", "Random2Class::RandomRanged (Phobos: Random2Class_RandomRanged_SyncLog)"),
    ("0x689eb0", "ScenarioClass::ReadMap (Phobos: ScenarioClass_ReadMap_SkipHeaderInCampaign)"),
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
print("RANDMAP_PROBE_DONE")
