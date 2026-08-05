# -*- coding: utf-8 -*-
"""反编译 HouseClass::GetPowerPercentage + 检查全局数据"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.data import FloatDataType, DoubleDataType, IntegerDataType
import codecs

ADDRS = [
    ("0x4fce30", "HouseClass::GetPowerPercentage"),
]

OUT = r"E:\code\ra2-reverse\power_percentage_decomp.txt"
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

# 全局数据探测
f.write("=" * 70 + "\n")
f.write("## 全局数据探测\n")
listing = currentProgram.getListing()
dm = currentProgram.getDataTypeManager()

def read_float(base_addr, off):
    a = af.getAddress(base_addr).add(off)
    try:
        d = listing.getDataAt(a)
        return str(d)
    except Exception as e:
        return "ERR %s" % e

f.write("0x7e1748 (float): %s\n" % read_float("0x7e1748", 0))
f.write("0x8871e0+0x57c (float): %s\n" % read_float("0x8871e0", 0x57c))
f.write("0x8871e0+0x57c+4 (float): %s\n" % read_float("0x8871e0", 0x580))

f.close()
decomp.dispose()
print("POWERPCT_DECOMP_DONE")
