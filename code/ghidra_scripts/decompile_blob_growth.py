# -*- coding: utf-8 -*-
"""涂块核心 FUN_0059BBC0 的下挂实现：优先队列扩散所需的全部零件

已知骨架：以一个随机起点为种子，用「最小堆 + 优先级」向外长一团陆地，
优先级 = 到一个「会随机漂移的中心」的距离 + 随机抖动，并被限制在椭圆内。
本脚本把优先级函数、椭圆判定、漂移量、堆操作、取格函数全部反编译出来。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_blob_growth.txt")

TARGETS = [
    ("0x5980c0", "中心漂移量（每步给中心加的随机增量）"),
    ("0x598030", "随机浮点基元（0x5980C0 的下层）"),
    ("0x59bab0", "椭圆内判定"),
    ("0x59b940", "优先级函数（param_11 != 0 分支）"),
    ("0x5ad870", "最小堆下沉"),
    ("0x5ada40", "堆/池前置初始化"),
    ("0x5657a0", "按坐标取 CellClass"),
    ("0x4865d0", "取格可用性谓词"),
    ("0x57a0c0", "涂块后校验 + 工作数组分配"),
    ("0x4cac40", "sqrt 包装"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

f = codecs.open(OUT, "w", "utf-8")
f.write("## 涂块核心零件反编译\n")
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
    f.write(res.getDecompiledFunction().getC() if res.decompileCompleted()
            else "// 反编译失败: %s\n" % res.getErrorMessage())
f.close()
print("written: " + OUT)
