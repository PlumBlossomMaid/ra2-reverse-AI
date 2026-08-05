# -*- coding: utf-8 -*-
"""反编译 TemporalClass 移除链路 + BuildingClass 驻军处理 (v2)
用于排查: 动员兵驻军建筑 -> 超时空冻结建筑 -> 建筑消失 -> 崩溃
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_temporal_crash.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.program.model.symbol import SourceType
import codecs

ADDRS = [
    ("0x71a5a0", "TemporalClass::AI? (尝试创建函数)"),
    ("0x71a780", "TemporalClass::AI? (尝试创建函数)"),
    ("0x71af20", "TemporalClass::Fire"),
    ("0x71ade0", "TemporalClass::Detach"),
    ("0x71abc0", "TemporalClass::LetGo"),
    ("0x71ad40", "TemporalClass::JustLetGo"),
    ("0x71ae50", "TemporalClass::CanWarpTarget"),
    ("0x457de0", "BuildingClass::RemoveOccupants 大函数 (FUN_00457de0)"),
    ("0x458200", "BuildingClass::FUN_00458200"),
    ("0x4585c0", "BuildingClass::KillOccupants"),
    ("0x458e50", "BuildingClass::UpdateBunker"),
    ("0x4521c0", "BuildingClass::DisableTemporal"),
    ("0x452210", "BuildingClass::EnableTemporal"),
]

OUT = r"E:\code\ra2-reverse\temporal_crash_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    if fn is None or fn.getEntryPoint() != addr:
        if fn is not None and fn.getEntryPoint() != addr:
            f.write("NOTE: %s is inside %s @ %s\n" % (s, fn.getName(), fn.getEntryPoint()))
        # 在地址处创建函数
        cmd = CreateFunctionCmd(label, addr, None, SourceType.USER_DEFINED)
        if not cmd.applyTo(currentProgram):
            f.write("CreateFunctionCmd FAILED at %s (%s)\n" % (s, label))
        fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        f.write("NO FUNCTION at %s (%s)\n" % (s, label))
        continue
    f.write("## %s  %s  (entry: %s)\n" % (label, s, fn.getEntryPoint()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("TEMPORAL_CRASH_DECOMP_V2_DONE")
