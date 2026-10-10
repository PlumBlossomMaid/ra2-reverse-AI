# -*- coding: utf-8 -*-
"""导出全程序重建首批函数的反编译与逐指令证据。"""
import codecs
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "callgraph", "frontier_batch_057.txt")
TARGETS = [0x7CA7D8, 0x7C8EF4, 0x7D482C, 0x7DD0F8, 0x7DCFC4]

fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()
listing = currentProgram.getListing()
di = DecompInterface()
di.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
f = codecs.open(OUT, "w", "utf-8")

for value in TARGETS:
    addr = af.getAddress("0x%X" % value)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        fn = fm.getFunctionContaining(addr)
    f.write(u"\n" + u"=" * 78 + u"\n")
    if fn is None:
        f.write(u"0x%08X\tNO_FUNCTION\n" % value)
        continue
    entry = fn.getEntryPoint().getOffset()
    f.write(u"0x%08X\t%s\tbytes=%d%s\n" % (
        value, fn.getName(), fn.getBody().getNumAddresses(),
        u"" if entry == value else u"\tcontaining=0x%08X" % entry))
    f.write(u"-- assembly --\n")
    instructions = listing.getInstructions(fn.getBody(), True)
    while instructions.hasNext():
        ins = instructions.next()
        f.write(u"0x%08X  %s\n" % (ins.getAddress().getOffset(), ins.toString()))
    f.write(u"-- decompile --\n")
    result = di.decompileFunction(fn, 120, mon)
    if result.decompileCompleted():
        f.write(result.getDecompiledFunction().getC())
    else:
        f.write(u"(decompile failed: %s)\n" % result.getErrorMessage())

f.close()
print("written: %s" % OUT)
