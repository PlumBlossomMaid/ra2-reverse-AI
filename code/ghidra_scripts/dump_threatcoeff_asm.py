# -*- coding: utf-8 -*-
"""dump TechnoClass::ThreatCoefficients (0x70cd10) 汇编，精确还原浮点公式"""
import codecs

OUT = r"E:\code\ra2-reverse\threatcoeff_asm.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()

addr = af.getAddress("0x70cd10")
fn = currentProgram.getFunctionManager().getFunctionAt(addr)
end = fn.getBody().getMaxAddress().getOffset()

a = addr
count = 0
while a.getOffset() <= end and count < 500:
    insn = listing.getInstructionAt(a)
    if insn is None:
        a = a.add(1)
        continue
    f.write("0x%08x  %s\n" % (a.getOffset(), insn.toString()))
    a = a.add(insn.getLength())
    count += 1

f.close()
print("THREATCOEFF_ASM_DONE")
