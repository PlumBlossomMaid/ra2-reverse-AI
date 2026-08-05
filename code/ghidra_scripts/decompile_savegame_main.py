# -*- coding: utf-8 -*-
"""反编译 ScenarioClass::SaveGame 区 (0x67cef0 附近, Phobos SaveGame hooks)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_savegame_main.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x67cef0", "ScenarioClass_SaveGame 区 (Phobos hook AdjustMPSaveFileName)"),
    ("0x67d04e", "GameSave_SavegameInformation (Phobos hook)"),
    ("0x67d32c", "SaveGame_Phobos (Phobos hook)"),
    ("0x67e685", "LoadGame_PostSwizzle_Phobos (Phobos hook)"),
]

OUT = r"E:\code\ra2-reverse\savegame_main_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

# 先探测各 hook 地址所属函数
f.write("=" * 70 + "\n## 探测 hook 点所属函数\n")
for s, label in ADDRS:
    fn = fm.getFunctionContaining(af.getAddress(s))
    if fn is None:
        f.write("%s: NOT IN ANY FUNCTION\n" % s)
    else:
        body = fn.getBody()
        f.write("%s: %s @ %s [%s - %s]\n" % (s, fn.getName(), fn.getEntryPoint(),
                                              body.getMinAddress(), body.getMaxAddress()))
f.write("\n")

decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 对每个地址：若在函数内则反编译该函数
done = set()
for s, label in ADDRS:
    fn = fm.getFunctionContaining(af.getAddress(s))
    if fn is None:
        continue
    entry = str(fn.getEntryPoint())
    if entry in done:
        continue
    done.add(entry)
    f.write("=" * 70 + "\n## %s  (containing %s)\n" % (fn.getName(), s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SAVEGAME_MAIN_DECOMP_DONE")
