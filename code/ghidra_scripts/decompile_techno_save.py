# -*- coding: utf-8 -*-
"""TechnoClass 层序列化主体 FUN_004DB690 + UnitClass::Load/GetClassID 探测
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_techno_save.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.program.model.symbol import SourceType
import codecs

OUT = r"E:\code\ra2-reverse\techno_save_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

def decompile_at(s, label):
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        fn = fm.getFunctionContaining(addr)
        if fn is not None and fn.getEntryPoint() != addr:
            f.write("NOTE: %s inside %s @ %s\n" % (s, fn.getName(), fn.getEntryPoint()))
        else:
            cmd = CreateFunctionCmd(label, addr, None, SourceType.USER_DEFINED)
            if not cmd.applyTo(currentProgram):
                f.write("CreateFunctionCmd FAILED at %s\n" % s)
            fn = fm.getFunctionAt(addr)
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    if fn is None:
        f.write("NO FUNCTION\n")
        return
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

decompile_at("0x4db690", "FUN_004DB690 TechnoClass 层序列化主体")
decompile_at("0x744470", "UnitClass::Load (vtable+0x14)")
decompile_at("0x746de0", "UnitClass::GetClassID (vtable+0xC)")

f.close()
decomp.dispose()
print("TECHNO_SAVE_DECOMP_DONE")
