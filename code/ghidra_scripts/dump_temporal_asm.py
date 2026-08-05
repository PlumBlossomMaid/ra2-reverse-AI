# -*- coding: utf-8 -*-
"""dump TemporalClass 关键区汇编: AI 循环区 + Fire 尾部
范围1: 0x71a590-0x71ab30 (AI 主体, Ghidra 未建函数边界)
范围2: 0x71af20-0x71b1c0 (Fire 全范围, 含 Phobos hook 点 0x71b151)
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_temporal_asm.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

RANGES = [
    ("0x71a590", "0x71ab30", "TemporalClass AI 区"),
    ("0x71af20", "0x71b1c0", "TemporalClass Fire 区"),
]

OUT = r"E:\code\ra2-reverse\temporal_ai_asm.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()

for s, e, label in RANGES:
    start = af.getAddress(s).getOffset()
    end = af.getAddress(e).getOffset()
    f.write("=" * 70 + "\n## %s  [%s - %s]\n" % (label, s, e))
    a = af.getAddress(s)
    count = 0
    while a.getOffset() <= end and count < 1000:
        insn = listing.getInstructionAt(a)
        if insn is None:
            a = a.add(1)
            continue
        f.write("0x%08x  %s\n" % (a.getOffset(), insn.toString()))
        a = a.add(insn.getLength())
        count += 1
    f.write("\n")

f.close()
print("TEMPORAL_AI_ASM_DONE")
