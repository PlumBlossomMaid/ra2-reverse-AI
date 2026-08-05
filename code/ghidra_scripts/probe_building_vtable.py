# -*- coding: utf-8 -*-
"""确认 BuildingClass vtable 关键槽位指向的函数
槽位: +0x2c (WhatAmI) / +0x408 (AI 驻军计数) / +0xf8 (AI 移除目标)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript probe_building_vtable.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs
import re

OUT = r"E:\code\ra2-reverse\building_vtable_probe.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()

decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 1. 反编译 BuildingClass 构造函数找 vtable 写入
addr = af.getAddress("0x43b740")
fn = fm.getFunctionAt(addr)
res = decomp.decompileFunction(fn, 60, monitor)
c_code = res.getDecompiledFunction().getC() if res.decompileCompleted() else "FAILED"

candidates = set()
for m in re.finditer(r"0x[0-9a-fA-F]{6,8}", c_code):
    v = int(m.group(0), 16)
    if 0x7e0000 <= v <= 0x820000:
        candidates.add(v)
f.write("== CTOR 0x43b740 反编译候选 .rdata 地址 ==\n")
for v in sorted(candidates):
    f.write("  0x%08x\n" % v)

# 2. 对每个候选读槽位
SLOTS = [0x2c, 0xf8, 0x408]
f.write("\n== 槽位探测 ==\n")
for v in sorted(candidates):
    try:
        a = af.getAddress("0x%x" % v)
        row = []
        for slot in SLOTS:
            tgt = mem.getInt(a.add(slot), False)
            tfn = fm.getFunctionAt(af.getAddress("0x%08x" % tgt))
            name = tfn.getName() if tfn else "?"
            row.append("+0x%03x -> 0x%08x %s" % (slot, tgt, name))
        f.write("  vtable 0x%08x: %s\n" % (v, " | ".join(row)))
    except Exception as e:
        f.write("  vtable 0x%08x: ERR %s\n" % (v, e))

f.close()
decomp.dispose()
print("BUILDING_VTABLE_PROBE_DONE")
