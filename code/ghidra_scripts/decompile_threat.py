# -*- coding: utf-8 -*-
"""导出威胁评估系统相关函数反编译"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

ADDRS = [
    ("0x70cd10", "TechnoClass::ThreatCoefficients"),
    ("0x70f6e0", "TechnoClass::UpdateThreatInCell"),
    ("0x6f8df0", "TechnoClass::GreatestThreat"),
    ("0x56bcd0", "MapClass::GetThreatPosed"),
    ("0x509130", "HouseClass::AcquiredThreatNode"),
    ("0x509400", "HouseClass::AdjustThreats"),
    ("0x772a90", "WeaponTypeClass::AllowedThreats"),
    ("0x481870", "CellClass::UpdateThreat"),
    ("0x4d9920", "FootClass::GreatestThreat"),
]

OUT = r"E:\code\ra2-reverse\threat_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        cmd = CreateFunctionCmd(addr)
        if cmd.applyTo(currentProgram, monitor):
            fn = fm.getFunctionAt(addr)
            f.write("[created function at %s]\n" % s)
        else:
            f.write("NO FUNCTION at %s (%s)\n" % (s, label))
            continue
    f.write("=" * 70 + "\n")
    f.write("## %s  %s\n" % (label, s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("THREAT_DECOMP_DONE")
