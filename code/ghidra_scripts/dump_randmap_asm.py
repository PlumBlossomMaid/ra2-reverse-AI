# -*- coding: utf-8 -*-
"""dump 随机地图生成器区域 (0x597000-0x598000) 完整汇编
背景：xref 显示 0x5978xx-0x597Exx 密集引用 RandMap.img / *.mmp / RandMap.Sed，
     是 MapGeneratorClass 方法区（Ghidra 未建函数），逐条汇编还原。
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript dump_randmap_asm.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "randmap_gen_asm.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
fm = currentProgram.getFunctionManager()

START = "0x597000"
END = "0x598000"
f.write("## 随机地图生成器区域汇编 [%s - %s]\n" % (START, END))

start = af.getAddress(START)
end = af.getAddress(END)

# 先标出已识别的函数边界
f.write("\n-- 区域内已识别函数 --\n")
it = fm.getFunctions(start, True)
for fn in it:
    addr = fn.getEntryPoint()
    if addr.compareTo(end) >= 0:
        break
    f.write("%s\t%s\t[%s - %s]\n" % (addr, fn.getName(), fn.getEntryPoint(),
                                     fn.getBody().getMaxAddress()))

f.write("\n-- 指令流 --\n")
it = listing.getInstructions(start, True)
count = 0
while it.hasNext():
    inst = it.next()
    a = inst.getAddress()
    if a.compareTo(end) >= 0:
        break
    # 标记指令是否属于某函数
    fn = fm.getFunctionContaining(a)
    tag = ("[%s]" % fn.getName()) if fn else "[NOFN]"
    f.write("%s\t%s\t%s\n" % (a, tag, inst))
    count += 1

f.write("\n-- 共 %d 条指令 --\n" % count)
f.close()
print("RANDMAP_ASM_DONE")
