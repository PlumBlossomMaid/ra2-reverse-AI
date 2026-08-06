# -*- coding: utf-8 -*-
"""Seeding water 阶段全套函数反编译

阶段调度：FUN_0059A6C0 按 MapSeedClass+0x3C(MapType) 三分支：
  MapType 0 -> FUN_0059AD10 / 1 -> FUN_0059AFA0 / 2 -> FUN_0059B200
辅助两步：FUN_0059C580（MapType 3/4 且 WaterAmount>0x14 时的额外撒点）
          FUN_0059C630（把水格细化成具体水面 tile）
再往下：FUN_0059D510 / FUN_0059C920 / FUN_005A6C10 是撒点与连通处理的实现。

运行: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
      -postScript decompile_water_stage.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_water_stage.txt")

TARGETS = [
    ("0x59a6c0", "Seeding water 调度（按 MapType 三分支）"),
    ("0x59ad10", "MapType 0 水域布局"),
    ("0x59afa0", "MapType 1 水域布局"),
    ("0x59b200", "MapType 2 水域布局"),
    ("0x59c580", "MapType 3/4 追加撒点（WaterAmount>0x14）"),
    ("0x59c630", "水面 tile 细化"),
    ("0x59d510", "撒点实现 A"),
    ("0x59c920", "撒点实现 B"),
    ("0x5a6c10", "区域/连通处理"),
    ("0x59a8f0", "水域辅助（同源文件邻域）"),
    ("0x578e60", "工作记录数组分配（side^2 x 0x50）"),
    ("0x58bdc0", "工作记录构造"),
    ("0x58c2c0", "工作记录按索引取址"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## Seeding water 阶段反编译\n")

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
        f.write("// 反编译失败: %s\n" % res.getErrorMessage())

f.close()
print("written: " + OUT)
