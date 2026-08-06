# -*- coding: utf-8 -*-
"""Randomizer(R250) 三函数汇编逐条 dump —— 字节级复刻的地基

反编译对移位符号性(SAR vs SHR)、循环移位(ROR/ROL)会失真，必须汇编核对：
  0x65C6D0 Randomizer::Randomizer  播种(4 轮 Feistel 混淆 -> 250 状态字)
  0x65C780 Randomizer::Random      R250 取数
  0x65C7E0 Randomizer::RandomRanged 区间随机(拒绝采样)
运行: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
      -postScript dump_randomizer_asm.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "randomizer_asm.txt")

RANGES = [
    ("0x65c6d0", "0x65c780", "Randomizer::Randomizer (播种)"),
    ("0x65c780", "0x65c7e0", "Randomizer::Random (R250)"),
    ("0x65c7e0", "0x65c890", "Randomizer::RandomRanged"),
]

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
f = codecs.open(OUT, "w", "utf-8")

for lo_s, hi_s, label in RANGES:
    lo = af.getAddress(lo_s)
    hi = af.getAddress(hi_s)
    f.write("=" * 70 + "\n## %s  [%s - %s]\n" % (label, lo_s, hi_s))
    it = listing.getInstructions(lo, True)
    while it.hasNext():
        inst = it.next()
        a = inst.getAddress()
        if a.compareTo(hi) >= 0:
            break
        b = inst.getBytes()
        hexs = " ".join(["%02x" % (x & 0xFF) for x in b])
        f.write("%s  %-24s  %s\n" % (a, hexs, inst))
    f.write("\n")

f.close()
print("RANDOMIZER_ASM_DONE -> " + OUT)
