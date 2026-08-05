# -*- coding: utf-8 -*-
"""探测关键地址所属的函数边界
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript probe_containing_func.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

ADDRS = [
    "0x71a450", "0x71a4e0", "0x71a780", "0x71a7bc", "0x71a82c",
    "0x71a88d", "0x71a8bd", "0x71ab10",
    "0x458000", "0x458060", "0x458100", "0x458148", "0x458180", "0x458200",
    "0x4585c0",
]

OUT = r"E:\code\ra2-reverse\probe_containing.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

for s in ADDRS:
    addr = af.getAddress(s)
    fn = fm.getFunctionContaining(addr)
    if fn is None:
        f.write("%s\tNOT IN ANY FUNCTION\n" % s)
    else:
        body = fn.getBody()
        f.write("%s\tcontained in: %s @ %s  [%s - %s]\n" % (
            s, fn.getName(), fn.getEntryPoint(), body.getMinAddress(), body.getMaxAddress()))

f.close()
print("PROBE_CONTAINING_DONE")
