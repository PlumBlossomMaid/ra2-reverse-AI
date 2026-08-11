# -*- coding: utf-8 -*-
"""列出含指定关键字的符号并反编译其函数（LCW/LZO/Pack 相关）。

用法: RA2_SYM=LCW RA2_OUT=lcw_syms.txt analyzeHeadless ... -postScript dump_syms.py
"""
import codecs
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.program.model.symbol import SymbolType
from ghidra.util.task import ConsoleTaskMonitor

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
kw = os.environ.get("RA2_SYM", "LCW")
name = os.environ.get("RA2_OUT", "syms.txt")
OUT = os.path.join(_REPO, "memory", "data", "decomp", name)

di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()

f = codecs.open(OUT, "w", "utf-8")
found = []
for sym in st.getAllSymbols(True):
    sname = sym.getName()
    if kw.lower() in sname.lower() and sym.getSymbolType() == SymbolType.FUNCTION:
        found.append(sym)
found.sort(key=lambda s: s.getAddress().getOffset())
f.write(u"## %d functions match '%s'\n" % (len(found), kw))
for sym in found:
    addr = sym.getAddress()
    fn = fm.getFunctionAt(addr)
    f.write(u"\n### %s @ 0x%X\n" % (sym.getName(), addr.getOffset()))
    if fn is not None:
        r = di.decompileFunction(fn, 90, mon)
        f.write(r.getDecompiledFunction().getC() if r.decompileCompleted() else u"(失败)\n")
f.close()
print("written: %s (%d funcs)" % (OUT, len(found)))
