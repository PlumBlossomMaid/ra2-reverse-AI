# -*- coding: utf-8 -*-
"""dump 0x89E3A0 写入点 0x52F7AF 上下文汇编（判断是否被免CD补丁修改）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_cdcheck_patch.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "cdcheck_patch_asm.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

# 0x52F7AF 所在函数 + 前后 0x100 字节
fn = fm.getFunctionContaining(af.getAddress("0x52f7af"))
if fn is not None:
    f.write("## fn containing 0x52F7AF: %s @ %s [%s - %s]\n\n" % (
        fn.getName(), fn.getEntryPoint(), fn.getEntryPoint(), fn.getBody().getMaxAddress()))

for s, e, label in [("0x52f6f0", "0x52f850", "0x89E3A0 写入点上下文")]:
    start = af.getAddress(s)
    end = af.getAddress(e)
    f.write("## asm [%s - %s] %s\n" % (s, e, label))
    a = start
    while a is not None and a.compareTo(end) < 0:
        inst = listing.getInstructionAt(a)
        if inst is None:
            inst = listing.getDataAt(a)
        if inst is not None:
            f.write("%s\t%s\n" % (a, inst))
            a = a.add(inst.getLength())
        else:
            f.write("%s\t???\n" % a)
            a = a.add(1)
    f.write("\n")

# 原始字节对比：0x52F7AF 附近 32 字节的十六进制
f.write("## raw bytes around 0x52F7AF\n")
addr = af.getAddress("0x52f790")
mem = currentProgram.getMemory()
b = mem.getBytes(addr, 64)
f.write(" ".join("%02x" % (x & 0xFF) for x in b) + "\n")

f.close()
print("PATCH_ASM_DONE")
