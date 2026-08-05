# -*- coding: utf-8 -*-
"""游戏运行流第二轮反编译：WinMain(长超时) + 结束事件处理 + 菜单循环关键函数
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_gameloop2.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

# (函数内锚点地址, 标签, 超时秒)
ANCHORS = [
    ("0x6bb9a0", "WinMain (FUN_006bb9a0, 大函数长超时)", 300),
    ("0x6863e0", "EndEvent_3 (DAT_008b41c0 分支, 推测 DoRestart)", 60),
    ("0x686570", "EndEvent_4 (DAT_00a83d48 分支, Disconnect Gracefully)", 60),
    ("0x52d9a0", "MainGame 外层循环条件 (菜单循环?)", 60),
    ("0x48d080", "MainLoop 每帧回调 FUN_0048d080", 60),
    ("0x48c8b0", "MainGame 内 MainLoop 后每帧 FUN_0048c8b0", 60),
]

OUT = os.path.join(_REPO, "memory", "data", "decomp", "gameloop_decomp2.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label, tmo in ANCHORS:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        f.write("NO FUNCTION at %s (%s)\n" % (s, label))
        continue
    f.write("## %s  fn=%s @ %s [%s - %s]\n" % (
        label, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, tmo, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("GAMELOOP2_DECOMP_DONE")
