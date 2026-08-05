# -*- coding: utf-8 -*-
"""反编译存档系统: SavegameInformation + 保存主流程 + 魔数
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_savegame.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x680ff0", "SavegameInformation::SavegameInformation"),
    ("0x6812e0", "SavegameInformation::Write"),
    ("0x681840", "SavegameInformation::Read"),
]

OUT = r"E:\code\ra2-reverse\savegame_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

# 1. 读存档魔数
f.write("=" * 70 + "\n## Game::Savegame_Magic @ 0x83D560\n")
try:
    v = mem.getInt(af.getAddress("0x83d560"), False)
    f.write("dword: 0x%08x\n" % v)
    v2 = mem.getInt(af.getAddress("0x83d560"), True)
    f.write("dword(little): 0x%08x\n" % v2)
except Exception as e:
    f.write("ERR: %s\n" % e)

# 2. 探测 0x55dbcd 属于哪个函数 (MainLoop_SaveGame)
f.write("\n## probe 0x55dbcd (MainLoop_SaveGame hook 点)\n")
fn = fm.getFunctionContaining(af.getAddress("0x55dbcd"))
if fn is None:
    f.write("NOT IN ANY FUNCTION\n")
else:
    body = fn.getBody()
    f.write("%s @ %s [%s - %s]\n" % (fn.getName(), fn.getEntryPoint(),
                                      body.getMinAddress(), body.getMaxAddress()))
    ADDRS.append((str(fn.getEntryPoint()), "MainLoop_SaveGame containing: " + fn.getName()))

# 3. 反编译
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        f.write("NO FUNCTION at %s (%s)\n" % (s, label))
        continue
    f.write("## %s  %s\n" % (label, s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SAVEGAME_DECOMP_DONE")
