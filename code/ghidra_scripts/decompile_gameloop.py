# -*- coding: utf-8 -*-
"""导出游戏主循环/生命周期相关函数的反编译 C 伪代码
覆盖: WinMain(入口) / MainLoop(主循环) / 场景加载链
用 getFunctionContaining 锚定 Phobos hook 点所在函数，避免手工猜边界。
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_gameloop.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

# (函数内锚点地址, 标签) —— Phobos hook 点 / YRpp 已知地址
ANCHORS = [
    ("0x6bd68d", "WinMain (Phobos: WinMain_PhobosRegistrations)"),
    ("0x48ce85", "MainGame (Phobos: MainGame_ShowBriefing)"),
    ("0x55d14f", "AuxLoop (Phobos: AuxLoop_ShowBriefing)"),
    ("0x55d360", "MainLoop (Phobos: MainLoop_FrameStep_Begin)"),
    ("0x683e41", "ScenarioClass::Start (Phobos: ScenarioClass_Start_ShowBriefing)"),
    ("0x683f66", "PauseGame (Phobos: PauseGame_ShowBriefing)"),
    ("0x685659", "Scenario_ClearClasses"),
    ("0x685d95", "DoWin (Phobos: DoWin_ShowBriefing)"),
    ("0x686092", "DoLose (Phobos: DoLose_RetryDialogForCampaigns)"),
    ("0x6870d7", "ReadScenario_MissionINI"),
    ("0x69bae7", "SessionClass_Resume_CampaignGameSpeed"),
]

OUT = os.path.join(_REPO, "memory", "data", "decomp", "gameloop_decomp.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ANCHORS:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n")
    if fn is None:
        f.write("NO FUNCTION containing %s (%s)\n" % (s, label))
        continue
    f.write("## %s  anchor=%s  fn=%s @ %s [%s - %s]\n" % (
        label, s, fn.getName(), fn.getEntryPoint(),
        fn.getEntryPoint(), fn.getBody().getMaxAddress()))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("GAMELOOP_DECOMP_DONE")
