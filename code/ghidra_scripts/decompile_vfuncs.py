# -*- coding: utf-8 -*-
"""反编译 TimeToBuild 依赖的三个虚函数 + 用 getInt 读全局浮点"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import struct, codecs

ADDRS = [
    ("0x6f3270", "TechnoClass vtable[0x84] -> 可能是 GetType/GetCost"),
    ("0x4e0130", "TechnoClass vtable[0x88] -> TimeToBuild 第一个调用"),
    ("0x4c9150", "TechnoClass vtable[0x2c] -> WhatAmI?"),
]

OUT = r"E:\code\ra2-reverse\vtable_funcs_decomp.txt"
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
            f.write("FAILED to create at %s (%s)\n" % (s, label))
            continue
    f.write("=" * 70 + "\n")
    f.write("## %s  %s\n" % (label, s))
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

# 全局浮点：用 getInt 读 uint 再转 float
mem = currentProgram.getMemory()
def get_uint(v):
    return mem.getInt(af.getAddress("0x%x" % v), False) & 0xffffffff

f.write("=" * 70 + "\n## 全局常量（getInt 方式）\n")
for base_va, off, desc in [
    (0x7e1748, 0, "TimeToBuild 比较左值(_DAT_007e1748)"),
    (0x8871e0, 0x57c, "RulesClass::Instance+0x57c"),
    (0x7e1718, 0, "GetPowerPercentage 电力充足返回值"),
    (0x7e2800, 0, "GetPowerPercentage 断电返回值"),
    (0x8871e0, 0xf0, "RulesClass+0xf0 (队列上限)"),
]:
    try:
        u = get_uint(base_va + off)
        flt = struct.unpack("<f", struct.pack("<I", u))[0]
        f.write("  0x%08x+0x%03x: uint=0x%08x float=%g\n" % (base_va, off, u, flt))
    except Exception as e:
        f.write("  0x%08x+0x%03x: ERR %s\n" % (base_va, off, e))

f.close()
decomp.dispose()
print("VFUNC_DECOMP_DONE")
