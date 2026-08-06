# -*- coding: utf-8 -*-
"""RMG(随机地图生成器)流水线函数批量反编译

背景：FUN_00598960 是生成主流程，其内部调试日志字符串给出阶段名：
  Init random map / Seeding water / Init regions / Making regions /
  Recalculating cell attributes / Creating starting points /
  Adding tech buildings / Creating tiberium / Creating hills /
  Creating LATs, rocks etc / Cleanup / Compute Radar Image / Done
本脚本按阶段顺序反编译各阶段实现函数 + 随机数播种构造。
运行: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
      -postScript decompile_rmg_pipeline.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_pipeline_decomp.txt")

TARGETS = [
    ("0x598960", "RMG 生成主流程(阶段调度)"),
    ("0x65c6d0", "Randomizer::Randomizer 播种"),
    ("0x599650", "阶段: Init random map"),
    ("0x59a6c0", "阶段: Seeding water (主)"),
    ("0x59c580", "阶段: Seeding water (辅1)"),
    ("0x59c630", "阶段: Seeding water (辅2)"),
    ("0x59b740", "阶段: Recalculating cell attributes"),
    ("0x5a1fb0", "阶段: Creating starting points"),
    ("0x5a23a0", "阶段: Creating tiberium"),
    ("0x5a35f0", "阶段: Creating hills"),
    ("0x5a38c0", "阶段: Creating LATs/rocks (1)"),
    ("0x5a3ae0", "阶段: Creating LATs/rocks (2)"),
    ("0x5a4280", "阶段: Creating LATs/rocks (3)"),
    ("0x5a95b0", "阶段: Adding tech buildings"),
    ("0x595400", "中立建筑放置(Neutral + BuildingClass)"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## RMG 流水线函数反编译\n")

for sa, label in TARGETS:
    addr = af.getAddress(sa)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("\n" + "=" * 70 + "\n")
    f.write("## %s  @ %s  fn=%s\n\n" % (label, sa, fn.getName() if fn else "CREATE_FAILED"))
    if fn is None:
        continue
    res = decomp.decompileFunction(fn, 300, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    print("done " + sa)

f.close()
decomp.dispose()
print("RMG_PIPELINE_DECOMP_DONE -> " + OUT)
