# -*- coding: utf-8 -*-
"""第三轮：确认场景加载中间层 FUN_00684620 + 定位四个结束事件标志的写入点
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_gameloop3.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "gameloop_decomp3.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1) 反编译场景加载中间层
for s, label in [("0x684620", "ScenarioClass::Start -> 场景加载层 FUN_00684620")]:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n")
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

# 2) 定位结束事件标志的引用（写入点 = 胜负判定触发处）
FLAGS = [
    ("0xa83d49", "结束标志1 (胜利? -> DoWin)"),
    ("0xa8ecd0", "结束标志2 (失败? -> DoLose)"),
    ("0x8b41c0", "结束标志3 (投降? -> DoRestart)"),
    ("0xa83d48", "结束标志4 (断线/退出? -> Exit)"),
]
f.write("=" * 70 + "\n## 结束事件标志引用点\n")
refmgr = currentProgram.getReferenceManager()
for s, label in FLAGS:
    addr = af.getAddress(s)
    f.write("### %s %s\n" % (label, s))
    refs = refmgr.getReferencesTo(addr)
    for ref in refs:
        fromAddr = ref.getFromAddress()
        fn = fm.getFunctionContaining(fromAddr)
        rtype = "WRITE" if ref.getReferenceType().isWrite() else "read "
        f.write("  %s @ %s  %s\n" % (rtype, fromAddr, fn.getName() if fn else "?"))
    f.write("\n")

f.close()
decomp.dispose()
print("GAMELOOP3_DECOMP_DONE")
