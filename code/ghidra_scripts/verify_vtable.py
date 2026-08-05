# -*- coding: utf-8 -*-
"""验证 vtable 布局：dump TechnoClass/BuildingClass vtable 槽位 + WhatAmI 反编译"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs

OUT = r"E:\code\ra2-reverse\vtable_verify.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
mem = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()

def get_uint(v):
    return mem.getInt(af.getAddress("0x%x" % v), False) & 0xffffffff

def sym_at(v):
    try:
        s = st.getPrimarySymbol(af.getAddress("0x%x" % v))
        if s:
            return s.getName()
    except Exception:
        pass
    fn = fm.getFunctionAt(af.getAddress("0x%x" % v))
    if fn:
        return fn.getName()
    return "?"

for name, base in [("TechnoClass", 0x7f4960), ("BuildingClass", 0x0)]:
    f.write("## %s vtable @ 0x%x\n" % (name, base))
    # 0x7f4960 候选；BuildingClass 先不 dump（未确认地址）

f.write("\n## TechnoClass vtable @ 0x7f4960 槽位明细\n")
base = 0x7f4960
for off in range(0, 0x90, 4):
    t = get_uint(base + off)
    f.write("  [0x%02x] 0x%08x  %s\n" % (off, t, sym_at(t)))

# WhatAmI 候选：AbstractClass 布局假设 [0x14]
f.write("\n## 反编译 TechnoClass vtable[0x14]\n")
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()
target = get_uint(base + 0x14)
fn = fm.getFunctionAt(af.getAddress("0x%x" % target))
if fn is None:
    cmd = CreateFunctionCmd(af.getAddress("0x%x" % target))
    cmd.applyTo(currentProgram, monitor)
    fn = fm.getFunctionAt(af.getAddress("0x%x" % target))
if fn:
    res = decomp.decompileFunction(fn, 60, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED\n")
else:
    f.write("NO FUNCTION at 0x%x\n" % target)
f.close()
decomp.dispose()
print("VTABLE_VERIFY_DONE")
