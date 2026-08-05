# -*- coding: utf-8 -*-
"""dump TechnoClass vtable 槽位目标 + 全局浮点常量值"""
import struct, codecs

OUT = r"E:\code\ra2-reverse\vtable_slots.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
mem = currentProgram.getMemory()
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()

def addr(v):
    return af.getAddress("0x%x" % v)

def sym_at(v):
    try:
        s = st.getPrimarySymbol(addr(v))
        if s:
            return s.getName()
    except Exception:
        pass
    fn = fm.getFunctionAt(addr(v))
    if fn:
        return fn.getName()
    return "?"

def get_uint(v):
    return mem.getInt(addr(v), False) & 0xffffffff

def get_float(v):
    b = mem.getBytes(addr(v), 4)
    return struct.unpack("<f", bytes(b))[0]

f.write("## TechnoClass vtable @ 0x7f4960\n")
base = 0x7f4960
for off in [0x2c, 0x84, 0x88, 0x8c, 0x90, 0x94, 0x98, 0x9c, 0xa0, 0xa4, 0xa8, 0xac]:
    target = get_uint(base + off)
    f.write("  [0x%02x] -> 0x%08x  %s\n" % (off, target, sym_at(target)))

f.write("\n## 全局浮点/整型常量\n")
for base_va, off, desc in [
    (0x7e1748, 0, "TimeToBuild 比较左值(_DAT_007e1748)"),
    (0x8871e0, 0x57c, "RulesClass::Instance+0x57c (BuildSpeed?)"),
    (0x7e1718, 0, "GetPowerPercentage 电力充足返回值"),
    (0x7e2800, 0, "GetPowerPercentage 断电返回值"),
    (0x8871e0, 0xf0, "RulesClass+0xf0 (队列上限相关)"),
]:
    try:
        f.write("  0x%08x+0x%03x: float=%f  uint=0x%08x\n" % (
            base_va, off, get_float(base_va + off), get_uint(base_va + off)))
    except Exception as e:
        f.write("  0x%08x+0x%03x: ERR %s\n" % (base_va, off, e))

f.close()
print("SLOTS_DUMP_DONE")
