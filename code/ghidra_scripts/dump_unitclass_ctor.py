# -*- coding: utf-8 -*-
"""dump UnitClass 构造函数汇编, 找 vtable 写入指令
UnitClass::UnitClass @ 0x7353C0
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_unitclass_ctor.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

OUT = r"E:\code\ra2-reverse\unitclass_ctor_asm.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()

# 从 UnitClass 构造函数起点 dump 0x1000 字节指令
start = af.getAddress("0x7353c0")
end_off = start.getOffset() + 0x1000
a = start
count = 0
while a.getOffset() <= end_off and count < 3000:
    insn = listing.getInstructionAt(a)
    if insn is None:
        a = a.add(1)
        continue
    f.write("0x%08x  %s\n" % (a.getOffset(), insn.toString()))
    a = a.add(insn.getLength())
    count += 1

f.close()
print("UNITCLASS_CTOR_ASM_DONE")
