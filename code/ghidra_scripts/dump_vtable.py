# -*- coding: utf-8 -*-
"""找到 TechnoClass vtable 并 dump 关键槽位 + 反编译构造函数"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.program.model.symbol import SourceType
import codecs

OUT = r"E:\code\ra2-reverse\techno_vtable_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
mem = currentProgram.getMemory()

# 1. 反编译 TechnoClass 构造函数找 vtable
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

addr = af.getAddress("0x6f2b40")
fn = fm.getFunctionAt(addr)
res = decomp.decompileFunction(fn, 60, monitor)
c_code = res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAILED"

# 找 vtable 地址（形如 0x00xxxxxx 或 PTR_ 或 &LAB_）
import re
candidates = set()
for m in re.finditer(r"0x[0-9a-fA-F]{6,8}", c_code):
    v = int(m.group(0), 16)
    if 0x7e0000 <= v <= 0x820000:
        candidates.add(v)
f.write("== 构造函数反编译中的候选 .rdata/.data 地址 ==\n")
for v in sorted(candidates):
    f.write("  0x%08x\n" % v)

# 2. 用探测法找 vtable：TechnoClass 虚函数 0x84 槽位的值应该在 vtable 指向的数组
#    已知工厂类构造函数 FactoryClass::FactoryClass 里 vtable = PTR_FUN_007e88d0
#    直接对候选地址做启发：检查它是否以 AbstractClass 的 GetClassID 起始
f.write("\n== vtable 启发式扫描 ==\n")
for v in sorted(candidates):
    try:
        a = af.getAddress("0x%x" % v)
        d = listing.getDataAt(a)
        if d and d.getDataType() and "Pointer" in d.getDataType().getName():
            first = mem.getInt(a, False)
            f.write("  0x%08x: 第一个dword = 0x%08x\n" % (v, first))
    except Exception as e:
        f.write("  0x%08x: ERR %s\n" % (v, e))

f.close()
decomp.dispose()
print("VTABLE_DUMP_DONE")
