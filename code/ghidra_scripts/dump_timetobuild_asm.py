# -*- coding: utf-8 -*-
"""dump TechnoClass::TimeToBuild (0x6f47a0) 汇编，确认数学运算"""
import codecs

OUT = r"E:\code\ra2-reverse\timetobuild_asm.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()

addr = af.getAddress("0x6f47a0")
fn = currentProgram.getFunctionManager().getFunctionAt(addr)
end = fn.getBody().getMaxAddress().getOffset()

a = addr
count = 0
while a.getOffset() <= end and count < 300:
    insn = listing.getInstructionAt(a)
    if insn is None:
        a = a.add(1)
        continue
    f.write("0x%08x  %s\n" % (a.getOffset(), insn.toString()))
    a = a.add(insn.getLength())
    count += 1

f.close()
print("ASM_DUMP_DONE")
