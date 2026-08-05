# -*- coding: utf-8 -*-
"""反编译威胁评估核心：CanAutoTargetObject + TryAutoTargetObject + CalculateThreat(vtable[0x2c0])"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

OUT = r"E:\code\ra2-reverse\autotarget_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
mem = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

def get_uint(v):
    return mem.getInt(af.getAddress("0x%x" % v), False) & 0xffffffff

# 1. 找 TechnoClass vtable[0x2c0] 目标 (CalculateThreat)
f.write("## TechnoClass vtable[0x2c0] -> CalculateThreat 目标\n")
target = get_uint(0x7f4960 + 0x2c0)
f.write("vtable[0x2c0] -> 0x%08x\n\n" % target)

# 2. 反编译目标函数
ADDRS = [
    ("0x6f7ca0", "TechnoClass::CanAutoTargetObject"),
    ("0x6f8960", "TechnoClass::TryAutoTargetObject"),
    ("0x%x" % target, "TechnoClass::CalculateThreat (vtable[0x2c0])"),
]

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
print("AUTOTARGET_DECOMP_DONE")
