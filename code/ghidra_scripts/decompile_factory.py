# -*- coding: utf-8 -*-
"""导出 FactoryClass 生产系统相关函数的反编译 C 伪代码
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis -postScript decompile_factory.py
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x4c98b0", "FactoryClass::FactoryClass"),
    ("0x4c9c60", "FactoryClass::HasProgressChanged"),
    ("0x4c9c70", "FactoryClass::DemandProduction"),
    ("0x4c9e10", "FactoryClass::SetObject"),
    ("0x4c9e60", "FactoryClass::Suspend"),
    ("0x4c9ea0", "FactoryClass::Unsuspend"),
    ("0x4c9fb0", "FactoryClass::GetBuildTimeFrames"),
    ("0x4c9ff0", "FactoryClass::AbandonProduction"),
    ("0x4ca120", "FactoryClass::GetProgress"),
    ("0x4ca130", "FactoryClass::IsDone"),
    ("0x4ca180", "FactoryClass::GetCostPerStep"),
    ("0x4ca1a0", "FactoryClass::CompletedProduction"),
    ("0x4ca5a0", "FactoryClass::StartProduction"),
    ("0x4ca620", "FactoryClass::RemoveOneFromQueue"),
    ("0x4ca670", "FactoryClass::CountTotal"),
    ("0x4ca6b0", "FactoryClass::IsQueued"),
]

OUT = r"E:\code\ra2-reverse\factory_class_decomp.txt"
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
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("FACTORY_DECOMP_DONE")
