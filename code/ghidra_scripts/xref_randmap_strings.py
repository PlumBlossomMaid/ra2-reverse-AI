# -*- coding: utf-8 -*-
"""随机地图字符串 xref -> 引用函数反编译
查 RandMap.img / *.mmp / rmcache\\RandMap.Map / RandMap.Sed / ".SED" 的引用者。
找到读参数(.sed/.mmp)/写缓存(rmcache\\RandMap.Map)/读地形模板(RandMap.img)的
函数，即生成器入口或其直接调用者。
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript xref_randmap_strings.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs
import os

# 仓库根：脚本位于 <repo>/code/ghidra_scripts/，上两级即仓库根（不依赖环境注入）
_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

OUT = os.path.join(_REPO, "memory", "data", "decomp", "randmap_xref.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 字符串 VA（文件偏移经 PE 节表换算，_pe_off2va.py 输出）
TARGETS = [
    ("0x829abc", "RandMap.img"),
    ("0x82bb24", "*.mmp / rmcache\\"),
    ("0x82bb44", "rmcache\\RandMap.Map"),
    ("0x82bc30", "RandMap.Sed / lastmap.sed"),
    ("0x83da5e", '".SED" / Scen->IsRandom'),
]

for s, label in TARGETS:
    addr = af.getAddress(s)
    f.write("=" * 70 + "\n## %s @ %s\n" % (label, s))
    refs = list(currentProgram.getReferenceManager().getReferencesTo(addr))
    f.write("xrefs (%d):\n" % len(refs))
    for r in refs:
        f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
    f.write("\n")
    for r in refs:
        from_addr = r.getFromAddress()
        fn = fm.getFunctionContaining(from_addr)
        f.write("## ref from %s  fn=%s\n" % (
            from_addr, fn.getName() if fn else "NONE"))
        if fn is not None:
            res = decomp.decompileFunction(fn, 180, monitor)
            if res.decompileCompleted():
                f.write(res.getDecompiledFunction().getC())
            else:
                f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
        else:
            # 无函数包裹时汇编兜底
            a = from_addr.subtract(0x20)
            end = from_addr.add(0x40)
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

f.close()
decomp.dispose()
print("RANDMAP_XREF_DONE")
