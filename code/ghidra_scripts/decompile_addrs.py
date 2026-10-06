# -*- coding: utf-8 -*-
"""通用反编译器：地址与输出文件名从环境变量取，省得每次新建脚本。

用法（bash）：
  RA2_ADDRS=0x58d410,0x486380 RA2_OUT=rmg_foo.txt \
  analyzeHeadless.bat <proj> RA2 -process gamemd.exe -noanalysis \
      -postScript decompile_addrs.py -scriptPath <dir>

输出落在 memory/data/decomp/$RA2_OUT。
"""
import codecs
import os

from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
addrs = [s.strip() for s in os.environ.get("RA2_ADDRS", "").split(",") if s.strip()]
name = os.environ.get("RA2_OUT", "decomp_adhoc.txt")
OUT = os.path.join(_REPO, "memory", "data", "decomp", name)

di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()

f = codecs.open(OUT, "w", "utf-8")
for s in addrs:
    va = int(s, 16)
    addr = af.getAddress("0x%x" % va)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    if fn is None:
        f.write(u"\n## 0x%06X  (Ghidra 未定义该函数)\n" % va)
        continue
    f.write(u"\n## 0x%06X  fn=%s\n" % (va, fn.getName()))
    r = di.decompileFunction(fn, 120, mon)
    f.write(r.getDecompiledFunction().getC() if r.decompileCompleted() else u"(失败)\n")
f.close()
print("written: " + OUT)
