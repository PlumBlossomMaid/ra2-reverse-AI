# -*- coding: utf-8 -*-
"""探测指定地址区间的函数入口与名称
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_function_range.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

RANGES = [
    ("0x71a000", "0x71b300", "TemporalClass 区"),
    ("0x457f00", "0x459300", "BuildingClass 驻军区"),
]

OUT = r"E:\code\ra2-reverse\function_range_probe.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

for s, e, label in RANGES:
    start = af.getAddress(s)
    end = af.getAddress(e)
    f.write("=" * 70 + "\n## %s  [%s - %s]\n" % (label, s, e))
    it = fm.getFunctions(start, True)
    for fn in it:
        addr = fn.getEntryPoint()
        if addr.compareTo(end) > 0:
            break
        f.write("%s\t%s\n" % (addr, fn.getName()))
    f.write("\n")

f.close()
print("PROBE_DONE")
