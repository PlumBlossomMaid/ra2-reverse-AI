# -*- coding: utf-8 -*-
"""第二层取证: 加载流程 + 序列化批次完整列表 + 对象序列化基础
函数:
  FUN_0067d300 @ 0x67D300   保存序列化主体 (批次完整列表)
  FUN_0067e440 @ 0x67E440   加载主流程 (LoadGame)
  AbstractClass::Load @ 0x4103D0 附近 (对象序列化基础)
  FUN_004c6340 @ 0x4C6340   CONTENTS 头部写入?
  FUN_00674730 @ 0x674730   保存序列化第一步?
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_savegame_layer2.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x67d300", "FUN_0067d300 保存序列化主体"),
    ("0x67e440", "FUN_0067e440 加载主流程 (LoadGame)"),
    ("0x4103d0", "AbstractClass::Load 附近 (对象序列化基础)"),
    ("0x4c6340", "FUN_004c6340 CONTENTS 头部写入?"),
    ("0x674730", "FUN_00674730 保存第一步?"),
]

OUT = r"E:\code\ra2-reverse\savegame_layer2_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
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
    res = decomp.decompileFunction(fn, 90, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SAVEGAME_LAYER2_DECOMP_DONE")
