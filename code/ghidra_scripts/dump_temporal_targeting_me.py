# -*- coding: utf-8 -*-
"""dump TechnoClass::AI 中 TemporalTargetingMe 处理段汇编
范围: 0x51bb40-0x51bbd0 (InfantryClass::AI, Phobos hook 0x51BB6E)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_temporal_targeting_me.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

RANGES = [
    ("0x51bb40", "0x51bbd0", "InfantryClass::AI TemporalTargetingMe 段"),
]

OUT = r"E:\code\ra2-reverse\temporal_targeting_me_asm.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()

for s, e, label in RANGES:
    start = af.getAddress(s).getOffset()
    end = af.getAddress(e).getOffset()
    f.write("=" * 70 + "\n## %s  [%s - %s]\n" % (label, s, e))
    a = af.getAddress(s)
    count = 0
    while a.getOffset() <= end and count < 200:
        insn = listing.getInstructionAt(a)
        if insn is None:
            a = a.add(1)
            continue
        f.write("0x%08x  %s\n" % (a.getOffset(), insn.toString()))
        a = a.add(insn.getLength())
        count += 1
    f.write("\n")

f.close()
print("TEMPORAL_TARGETING_ME_ASM_DONE")
